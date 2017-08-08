
/*========================================================================*
 *                                                                        *
 *  Distributed by Whiteley Research Inc., Sunnyvale, California, USA     *
 *                       http://wrcad.com                                 *
 *  Copyright (C) 2017 Whiteley Research Inc., all rights reserved.       *
 *  Author: Stephen R. Whiteley, except as indicated.                     *
 *                                                                        *
 *  As fully as possible recognizing licensing terms and conditions       *
 *  imposed by earlier work from which this work was derived, if any,     *
 *  this work is released under the Apache License, Version 2.0 (the      *
 *  "License").  You may not use this file except in compliance with      *
 *  the License, and compliance with inherited licenses which are         *
 *  specified in a sub-header below this one if applicable.  A copy       *
 *  of the License is provided with this distribution, or you may         *
 *  obtain a copy of the License at                                       *
 *                                                                        *
 *        http://www.apache.org/licenses/LICENSE-2.0                      *
 *                                                                        *
 *  See the License for the specific language governing permissions       *
 *  and limitations under the License.                                    *
 *                                                                        *
 *   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,      *
 *   EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES      *
 *   OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-        *
 *   INFRINGEMENT.  IN NO EVENT SHALL WHITELEY RESEARCH INCORPORATED      *
 *   OR STEPHEN R. WHITELEY BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER     *
 *   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,      *
 *   ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE       *
 *   USE OR OTHER DEALINGS IN THE SOFTWARE.                               *
 *                                                                        *
 *========================================================================*
 *               XicTools Integrated Circuit Design System                *
 *                                                                        *
 * Misc. Utilities Library                                                *
 *                                                                        *
 *========================================================================*
 $Id:$
 *========================================================================*/

//
// General Microsoft Windows support functions.
//

#ifndef MSW_H
#define MSW_H
#ifdef WIN32

#include <winsock2.h>
#include <process.h>
#include <stdio.h>

namespace msw {
    bool IsWinNT();
    char *GetInstallDir(const char*);
    char *GetProgramRoot(const char*);
    bool GetProductID(char*, FILE*);
    char *AddPathDrives(const char*, const char*);
    PROCESS_INFORMATION *NewProcess(const char*, unsigned int, bool,
        void* = 0, void* = 0, void* = 0);
    const char *MapiSend(const char*, const char*, const char*, int = 0,
        const char** = 0);
    char *Billize(const char*);
    void UnBillize(char*);
    int ListPrinters(int*, char***);
    const char *RawFileToPrinter(const char*, const char*);

    // This must be defined in the application, and provides a suffix
    // used when creating application names as known in the registry.
    //
    extern const char *MSWpkgSuffix;
}

#endif
#endif
