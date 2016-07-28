#include "uv.h"
#include "internal.h"

static int uv__register_system_resume_callback();

void uv_init_detect_system_wakeup() {
  /* Try registering system power event callback. This is the cleanest
   * method, but it will only work on Win8 and above.
   */
  uv__register_system_resume_callback();
}

#define DEVICE_NOTIFY_CALLBACK 2

typedef ULONG CALLBACK DEVICE_NOTIFY_CALLBACK_ROUTINE(
  _In_opt_ PVOID Context,
  _In_ ULONG Type,
  _In_ PVOID Setting
);
typedef DEVICE_NOTIFY_CALLBACK_ROUTINE* PDEVICE_NOTIFY_CALLBACK_ROUTINE;

typedef struct _DEVICE_NOTIFY_SUBSCRIBE_PARAMETERS {
  PDEVICE_NOTIFY_CALLBACK_ROUTINE Callback;
  PVOID Context;
} DEVICE_NOTIFY_SUBSCRIBE_PARAMETERS, *PDEVICE_NOTIFY_SUBSCRIBE_PARAMETERS;

typedef PVOID HPOWERNOTIFY;
typedef HPOWERNOTIFY *PHPOWERNOTIFY;

typedef DWORD (WINAPI *sPowerRegisterSuspendResumeNotification)(
  _In_  DWORD         Flags,
  _In_  HANDLE        Recipient,
  _Out_ PHPOWERNOTIFY RegistrationHandle
);

static ULONG CALLBACK uv__system_resume_callback(PVOID Context, ULONG Type,
                                                 PVOID Setting) {
  if (Type == PBT_APMRESUMESUSPEND || Type == PBT_APMRESUMEAUTOMATIC) {
    uv_wake_all_loops();
  }
  return 0;
}

static int uv__register_system_resume_callback() {
  HMODULE powrprof_module;
  sPowerRegisterSuspendResumeNotification pPowerRegisterSuspendResumeNotification;
  DEVICE_NOTIFY_SUBSCRIBE_PARAMETERS recipient;
  HPOWERNOTIFY registration_handle;

  powrprof_module = LoadLibraryA("powrprof.dll");
  if (powrprof_module == NULL) {
    return 1;
  }

  pPowerRegisterSuspendResumeNotification = (sPowerRegisterSuspendResumeNotification)
    GetProcAddress(powrprof_module, "PowerRegisterSuspendResumeNotification");
  if (pPowerRegisterSuspendResumeNotification == NULL)
    return 1;

  recipient.Callback = uv__system_resume_callback;
  recipient.Context = NULL;
  return (*pPowerRegisterSuspendResumeNotification)(DEVICE_NOTIFY_CALLBACK,
                                                    &recipient,
                                                    &registration_handle);
}
