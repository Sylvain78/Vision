/* 
 * The contents of this file are subject to the Mozilla Public 
 * License Version 1.1 (the "License"); you may not use this file 
 * except in compliance with the License. You may obtain a copy of 
 * the License at http://www.mozilla.org/MPL/ 
 * 
 * Software distributed under the License is distributed on an "AS 
 * IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or 
 * implied. See the License for the specific language governing 
 * rights and limitations under the License. 
 * 
 * The Original Code is Vision.
 * 
 * The Initial Developer of the Original Code is The Vision Team.
 * Portions created by The Vision Team are
 * Copyright (C) 1999, 2000, 2001 The Vision Team.  All Rights
 * Reserved.
 * 
 * Contributor(s): Wade Majors <wade@ezri.org>
 *                 Rene Gollent
 *                 Todd Lair
 *                 Andrew Bazan
 *                 Jamie Wilkinson
 */
 
#include "MessageAgent.h"
#include "WindowList.h"
#include "ServerAgent.h"
#include "ClientWindow.h"
#include "StatusView.h"
#include "Vision.h"
#include "VTextControl.h"

#include <stdlib.h>

#ifdef BONE_BUILD
#include <sys/socket.h>
#include <arpa/inet.h>
#elif NETSERVER_BUILD
#include <socket.h>
#include <netdb.h>
#endif

MessageAgent::MessageAgent (
  BRect &frame_,
  const char *id_,
  int32 sid_,
  const char *serverName_,
  const BMessenger &sMsgr_,
  const char *nick,
  const char *addyString,
  bool chat,
  bool initiate,
  const char *IP,
  const char *port)

  : ClientAgent (
    id_,
    sid_,
    serverName_,
    nick,
    sMsgr_,
    frame_),

  chatAddy (addyString ? addyString : ""),
  chatee (id_),
  dIP (IP),
  dPort (port),
  dChat (chat),
  dInitiate (initiate),
  dConnected (false)
{
  Init();
}

MessageAgent::~MessageAgent (void)
{
  status_t result (0);
  dConnected = false;
  if (dChat)
  {
#ifdef BONE_BUILD
    shutdown (mySocket, SHUTDOWN_BOTH);
    shutdown (acceptSocket, SHUTDOWN_BOTH);
#endif
    wait_for_thread (dataThread, &result);
  }
}

void
MessageAgent::AttachedToWindow (void)
{
  // initialize threads here since messenger will otherwise not be valid
  if (dChat)
  {
    if (dInitiate)
      dataThread = spawn_thread(DCCIn, "DCC Chat(I)", B_NORMAL_PRIORITY, this);
    else
      dataThread = spawn_thread(DCCOut, "DCC Chat(O)", B_NORMAL_PRIORITY, this);
    
    resume_thread (dataThread);
  }
}

void
MessageAgent::Init (void)
{
// empty Init, call DCCServerSetup from input thread to avoid deadlocks
}

void
MessageAgent::DCCServerSetup(void)
{
  int32 myPort (1500 + (rand() % 5000));
  BString outNick (chatee);
  outNick.RemoveFirst (" [DCC]");
  struct sockaddr_in sa;
  
  BMessage reply;
  sMsgr.SendMessage (M_GET_IP, &reply);
  
  BString address;
  reply.FindString ("ip", &address); 
  
  if (dPort != "")
    myPort = atoi (dPort.String());

  mySocket = socket (AF_INET, SOCK_STREAM, 0);

  if (mySocket < 0)
  {
    LockLooper();
    Display ("Error creating socket.\n", 0);
    UnlockLooper();
    return;
  }

  sa.sin_family = AF_INET;
  inet_aton (address.String(), &sa.sin_addr);
  
  sa.sin_port = htons(myPort);
  
  if (bind (mySocket, (struct sockaddr*)&sa, sizeof(sa)) == -1)
  {
    myPort = 1500 + (rand() % 5000); // try once more
    sa.sin_port = htons(myPort);
    if (bind (mySocket, (struct sockaddr*)&sa, sizeof(sa)) == -1)
    {
      LockLooper();
      Display ("Error binding socket.\n", 0);
      UnlockLooper();
      return;
    }
  }
  
  BMessage sendMsg (M_SERVER_SEND);
  BString buffer;
  
  buffer << "PRIVMSG " << outNick << " :\1DCC CHAT chat ";
  buffer << htonl(sa.sin_addr.s_addr) << " ";
  buffer << myPort << "\1";
  sendMsg.AddString ("data", buffer.String());
  sMsgr.SendMessage (&sendMsg);
	
  listen (mySocket, 1);

  BString dataBuffer;
  struct in_addr addr;
		
  inet_aton (address.String(), &addr);
  dataBuffer << "Accepting connection on address "
    << address.String() << ", port " << myPort << "\n";
  LockLooper();
  Display (dataBuffer.String(), 0);
  UnlockLooper();
}

status_t
MessageAgent::DCCIn (void *arg)
{
  MessageAgent *agent ((MessageAgent *)arg);
  BMessenger sMsgrE (agent->sMsgr);
  BMessenger mMsgr (agent);
  
  agent->DCCServerSetup();
    
  int dccSocket (agent->mySocket);
  int dccAcceptSocket (0);
  
  struct sockaddr_in remoteAddy;
  int theLen (sizeof (struct sockaddr_in));

  dccAcceptSocket = accept(dccSocket, (struct sockaddr*)&remoteAddy, &theLen);

  if (dccAcceptSocket < 0)
    return B_ERROR;
    
  agent->acceptSocket = dccAcceptSocket;
  agent->dConnected = true;
  
  BMessage msg (M_DISPLAY);

  ClientAgent::PackDisplay (&msg, "Connected!\n", 0);
  mMsgr.SendMessage (&msg);

  char tempBuffer[2];
  BString inputBuffer;
  
  while (agent->dConnected)
  {
    if (recv(dccAcceptSocket, tempBuffer, 1, 0) == 0)
    {
      BMessage termMsg (M_DISPLAY);

      agent->dConnected = false;
      ClientAgent::PackDisplay (&termMsg, "DCC chat terminated.\n", 0);
      mMsgr.SendMessage (&termMsg);
      goto outta_there; // I hate goto, but this is a good use.
    }

    if (tempBuffer[0] == '\n')
      {
        agent->ChannelMessage (inputBuffer.String());
        inputBuffer = "";
      }
      else
        inputBuffer.Append(tempBuffer[0],1);
  }

	
  outta_there: // GOTO MARKER

#ifdef BONE_BUILD
  shutdown (dccSocket, SHUTDOWN_BOTH);
  shutdown (dccAcceptSocket, SHUTDOWN_BOTH);
#elif NETSERVER_BUILD
  closesocket (dccSocket);
  closesocket (dccAcceptSocket);
#endif

  return 0;
}

status_t
MessageAgent::DCCOut (void *arg)
{
  MessageAgent *agent ((MessageAgent *)arg);
  
  BMessenger mMsgr (agent);

  struct sockaddr_in sa;
  int status;
  int dccAcceptSocket (0);
  char *endpoint;
  
  uint32 realIP = strtoul (agent->dIP.String(), &endpoint, 10);

  if ((dccAcceptSocket = socket(AF_INET, SOCK_STREAM, 0)) < 0)
  {
    BMessage msg (M_DISPLAY);

    ClientAgent::PackDisplay (&msg, "Error opening socket.\n", 0);
    mMsgr.SendMessage (&msg);
    return false;
  }
  
  agent->acceptSocket = dccAcceptSocket;
	
  sa.sin_family = AF_INET;
  sa.sin_port = htons (atoi (agent->dPort.String()));
  sa.sin_addr.s_addr = ntohl (realIP);
  memset (sa.sin_zero, 0, sizeof(sa.sin_zero));

  {
    BMessage msg (M_DISPLAY);
    BString buffer;
    struct in_addr addr;

    addr.s_addr = ntohl (realIP);

    buffer << "Trying to connect to address "
      << inet_ntoa (addr)
      << " port " << agent->dPort << "\n";
    
    ClientAgent::PackDisplay (&msg, buffer.String(), 0);
    mMsgr.SendMessage (&msg);
  }

  status = connect (dccAcceptSocket, (struct sockaddr *)&sa, sizeof(sa));
  if (status < 0)
  {
    BMessage msg (M_DISPLAY);

    ClientAgent::PackDisplay (&msg, "Error connecting socket.\n", 0);
    mMsgr.SendMessage (&msg);
#ifdef BONE_BUILD
    shutdown (agent->mySocket, SHUTDOWN_BOTH);
#elif NETSERVER_BUILD
    closesocket (agent->mySocket);
#endif
    return false;
  }

  agent->dConnected = true;

  BMessage msg (M_DISPLAY);
  ClientAgent::PackDisplay (&msg, "Connected!\n", 0);
  mMsgr.SendMessage (&msg);

  char tempBuffer[2];
  BString inputBuffer;

  while (agent->dConnected)
  {
    if (recv (dccAcceptSocket, tempBuffer, 1, 0) == 0)
    {
      BMessage termMsg (M_DISPLAY);

      agent->dConnected = false;
      ClientAgent::PackDisplay (&termMsg, "DCC chat terminated.\n", 0);
      mMsgr.SendMessage (&termMsg);
      goto outta_loop; // I hate goto, but this is a good use.
    }

    if (tempBuffer[0] == '\n')
    {
      agent->ChannelMessage (inputBuffer.String());
      inputBuffer = "";
    }
    else
      inputBuffer.Append(tempBuffer[0],1);
  }
	
  outta_loop: // GOTO MARKER

#ifdef BONE_BUILD
  shutdown (dccAcceptSocket, SHUTDOWN_BOTH);
#elif NETSERVER_BUILD
  closesocket (dccAcceptSocket);
#endif  
  return 0;
}

void
MessageAgent::ChannelMessage (
  const char *msgz,
  const char *nick,
  const char *ident,
  const char *address)
{
//  agentWinItem->SetName (nick);

  ClientAgent::ChannelMessage (msgz, nick, ident, address);
}

void
MessageAgent::MessageReceived (BMessage *msg)
{
  switch (msg->what)
  {
    case M_CHANNEL_MSG:
      {
        const char *nick;

        if (msg->HasString ("nick"))
          msg->FindString ("nick", &nick);
        else
        {
          BString outNick (chatee.String());
          outNick.RemoveFirst (" [DCC]");
          msg->AddString ("nick", outNick.String());
        }
      
        if (myNick.ICompare (nick) != 0 && !dChat)
          agentWinItem->SetName (nick);      
      
        // Send the rest of processing up the chain
        ClientAgent::MessageReceived (msg);
      }
      break;
      
    case M_MSG_WHOIS:
      {
        BMessage dataSend (M_SERVER_SEND);

        AddSend (&dataSend, "WHOIS ");
        AddSend (&dataSend, chatee.String());
        AddSend (&dataSend, " ");
        AddSend (&dataSend, chatee.String());
        AddSend (&dataSend, endl);      
      }

    case M_CHANGE_NICK:
      {
        const char *oldNick, *newNick;

        msg->FindString ("oldnick", &oldNick);
        msg->FindString ("newnick", &newNick);

        if (chatee.ICompare (oldNick) == 0)
        {
          const char *address;
          const char *ident;

          msg->FindString ("address", &address);
          msg->FindString ("ident", &ident);

          BString oldId (id);
          chatee = id = newNick;
        
          agentWinItem->SetName (id.String());

          if (dChat)
            id.Append(" [DCC]");
       
          ClientAgent::MessageReceived (msg);
        }
      
        else if (myNick.ICompare (oldNick) == 0)
        {
          if (!IsHidden())
            vision_app->pClientWin()->pStatusView()->SetItemValue (STATUS_NICK, newNick);
          ClientAgent::MessageReceived (msg);
        }
      }
      break;

    case M_CLIENT_QUIT:
      {
        ClientAgent::MessageReceived(msg);
        BMessage deathchant (M_OBITUARY);
        deathchant.AddPointer ("agent", this);
        deathchant.AddPointer ("item", agentWinItem);
        vision_app->pClientWin()->PostMessage (&deathchant);
  
        deathchant.what = M_CLIENT_SHUTDOWN;
        sMsgr.SendMessage (&deathchant);
      }
      break;
     
    case M_STATUS_ADDITEMS:
      {
        vision_app->pClientWin()->pStatusView()->AddItem (new StatusItem (
            0, ""),
          true);
      
        vision_app->pClientWin()->pStatusView()->AddItem (new StatusItem (
            "Lag: ",
            "",
            STATUS_ALIGN_LEFT),
          true);
      
        vision_app->pClientWin()->pStatusView()->AddItem (new StatusItem (
            0,
            "",
            STATUS_ALIGN_LEFT),
          true);

        // The false bool for SetItemValue() tells the StatusView not to Invalidate() the view.
        // We send true on the last SetItemValue().
        vision_app->pClientWin()->pStatusView()->SetItemValue (STATUS_SERVER, serverName.String(), false);
        vision_app->pClientWin()->pStatusView()->SetItemValue (STATUS_LAG, myLag.String(), false);
        vision_app->pClientWin()->pStatusView()->SetItemValue (STATUS_NICK, myNick.String(), true);
      }        
      break;
    
    default:
      ClientAgent::MessageReceived (msg);
  }
}

void
MessageAgent::Parser (const char *buffer)
{
  if (!dChat)
  {
    BMessage dataSend (M_SERVER_SEND);

    AddSend (&dataSend, "PRIVMSG ");
    AddSend (&dataSend, chatee);
    AddSend (&dataSend, " :");
    AddSend (&dataSend, buffer);
    AddSend (&dataSend, endl);
  }
  else if (dConnected)
  {
    BString outTemp (buffer);

    outTemp << "\n";
    if (send(acceptSocket, outTemp.String(), outTemp.Length(), 0) < 0)
    {
      dConnected = false;
      Display ("DCC chat terminated.\n", 0);
      return;
    }
  }
  else
    return;

  BFont msgFont (vision_app->GetClientFont (F_TEXT));
  Display ("<", &myNickColor, &msgFont, vision_app->GetBool ("timestamp"));
  Display (myNick.String(), &nickdisplayColor);
  Display ("> ", &myNickColor);

  BString sBuffer (buffer);
  Display (sBuffer.String(), 0);
  

  Display ("\n", 0);
}

void
MessageAgent::DroppedFile (BMessage *)
{
  // TODO: implement this when DCC's ready
}

void
MessageAgent::TabExpansion (void)
{
  int32 start,
        finish;

  input->TextView()->GetSelection (&start, &finish);

  if (input->TextView()->TextLength()
  &&  start == finish
  &&  start == input->TextView()->TextLength())
  {
    const char *inputText (
                  input->TextView()->Text()
                  + input->TextView()->TextLength());
    const char *place (inputText);


    while (place > input->TextView()->Text())
    {
      if (*(place - 1) == '\x20')
        break;
      --place;
    }

    BString insertion;

    if (!id.ICompare (place, strlen (place)))
    {
      insertion = id;
      insertion.RemoveLast(" [DCC]");
    }
    else if (!myNick.ICompare (place, strlen (place)))
      insertion = myNick;

    if (insertion.Length())
    {
      input->TextView()->Delete (
        place - input->TextView()->Text(),
        input->TextView()->TextLength());

      input->TextView()->Insert (insertion.String());
      input->TextView()->Select (
        input->TextView()->TextLength(),
        input->TextView()->TextLength());
    }
  }
}
