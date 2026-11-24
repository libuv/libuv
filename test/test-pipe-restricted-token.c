/* Copyright libuv project contributors. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to
* deal in the Software without restriction, including without limitation the
* rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
* sell copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
* IN THE SOFTWARE.
*/


#include "uv.h"
#include "task.h"

#ifdef _WIN32

#include <aclapi.h>
#include <io.h>
#include <sddl.h>
#include <stdlib.h>
#include <string.h>

/* A restricted token like the ones sandboxes use: restricting SIDs Everyone,
 * Users and the logon SID, and a default DACL that grants SYSTEM, the user
 * and the logon SID full access (so objects created under it pass both
 * access checks). Under it, a pipe pair created by uv_pipe() must open: its
 * server end used to get the pipe file system's fixed default descriptor
 * (Everyone: read only), so the restricting SIDs could not open the other
 * end for writing.
 */
static HANDLE restricted_impersonation_token(void) {
  SID_AND_ATTRIBUTES restrict_sids[3];
  TOKEN_DEFAULT_DACL default_dacl;
  TOKEN_GROUPS* logon;
  TOKEN_USER* user;
  HANDLE restricted;
  HANDLE imp;
  HANDLE own;
  PSID everyone;
  PSID users;
  EXPLICIT_ACCESS_W ea[3];
  PACL acl;
  DWORD n;

  ASSERT(OpenProcessToken(GetCurrentProcess(),
                          TOKEN_DUPLICATE | TOKEN_QUERY | TOKEN_ADJUST_DEFAULT,
                          &own));

  n = 0;
  GetTokenInformation(own, TokenLogonSid, NULL, 0, &n);
  logon = malloc(n);
  ASSERT_NOT_NULL(logon);
  ASSERT(GetTokenInformation(own, TokenLogonSid, logon, n, &n));
  ASSERT_GT(logon->GroupCount, 0);

  n = 0;
  GetTokenInformation(own, TokenUser, NULL, 0, &n);
  user = malloc(n);
  ASSERT_NOT_NULL(user);
  ASSERT(GetTokenInformation(own, TokenUser, user, n, &n));

  ASSERT(ConvertStringSidToSidW(L"S-1-1-0", &everyone));
  ASSERT(ConvertStringSidToSidW(L"S-1-5-32-545", &users));
  restrict_sids[0].Sid = everyone;
  restrict_sids[0].Attributes = 0;
  restrict_sids[1].Sid = users;
  restrict_sids[1].Attributes = 0;
  restrict_sids[2].Sid = logon->Groups[0].Sid;
  restrict_sids[2].Attributes = 0;
  ASSERT(CreateRestrictedToken(own, 0, 0, NULL, 0, NULL, 3, restrict_sids,
                               &restricted));

  memset(ea, 0, sizeof(ea));
  ea[0].grfAccessPermissions = GENERIC_ALL;
  ea[0].grfAccessMode = SET_ACCESS;
  ea[0].Trustee.TrusteeForm = TRUSTEE_IS_SID;
  ea[0].Trustee.ptstrName = (LPWSTR) user->User.Sid;
  ea[1] = ea[0];
  ea[1].Trustee.ptstrName = (LPWSTR) logon->Groups[0].Sid;
  ea[2] = ea[0];
  ASSERT(ConvertStringSidToSidW(L"S-1-5-18",
                                (PSID*) &ea[2].Trustee.ptstrName));
  ASSERT_OK(SetEntriesInAclW(3, ea, NULL, &acl));

  ASSERT(DuplicateTokenEx(restricted,
                          TOKEN_QUERY | TOKEN_IMPERSONATE |
                              TOKEN_ADJUST_DEFAULT,
                          NULL,
                          SecurityImpersonation,
                          TokenImpersonation,
                          &imp));
  default_dacl.DefaultDacl = acl;
  ASSERT(SetTokenInformation(imp, TokenDefaultDacl, &default_dacl,
                             sizeof(default_dacl)));

  LocalFree(acl);
  LocalFree(ea[2].Trustee.ptstrName);
  LocalFree(everyone);
  LocalFree(users);
  CloseHandle(restricted);
  CloseHandle(own);
  free(user);
  free(logon);
  return imp;
}

#endif


TEST_IMPL(pipe_restricted_token) {
#ifndef _WIN32
  RETURN_SKIP("Windows only");
#else
  uv_file fds[2];
  HANDLE token;
  int r;

  token = restricted_impersonation_token();
  ASSERT(SetThreadToken(NULL, token));
  r = uv_pipe(fds, 0, 0);
  ASSERT(RevertToSelf());
  CloseHandle(token);

  ASSERT_OK(r);
  ASSERT_EQ(1, _write(fds[1], "x", 1));
  {
    char c;
    ASSERT_EQ(1, _read(fds[0], &c, 1));
    ASSERT_EQ('x', c);
  }
  ASSERT_OK(_close(fds[0]));
  ASSERT_OK(_close(fds[1]));

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
#endif
}
