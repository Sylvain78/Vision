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
 */

#ifndef _LOGGER_H_
#define _LOGGER_H_

#include <Locker.h>
#include <String.h>
#include <List.h>
#include <File.h>

class Logger
{
  public:
                       Logger (BString, BString);
                       ~Logger (void);
   void                Log (const char *);
   bool                isQuitting;
  
  private:
   void                SetupLogging (void);
   static int32        AsyncLogger (void *);
   thread_id           logThread;
   BString             logName;
   BString             serverName;
   BList               *logBuffer;
   BLocker             *logBufferLock;
   sem_id              logSyncherLock;
   BFile               *logFile;
};

#endif