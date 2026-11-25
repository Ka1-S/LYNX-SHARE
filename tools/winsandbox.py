"""Windows side of the UI monkey test (round 190rf, the 0.1.32 quality release): starts LYNX.exe on a desktop of its
own (never shown: nothing appears on the user's screen and no input reaches the user's desktop) as a low-integrity
process, so whatever the random clicks reach (Save as..., Delete in a file view, an export) cannot write outside the
run's own folder, which is labelled low-integrity for it. Windows has no read-only mount like the Linux sandbox
(scripts/ui-monkey.sh); a low-integrity process may only write to objects labelled low.

    proc = start_sandboxed([exe, "--demo", project], env, cwd=run_folder, desktop="lynx-monkey-<seed>", writable=[run_folder])
    proc.poll() / proc.wait(seconds) / proc.kill() / proc.pid / proc.returncode

Standard library only (ctypes), so it runs under the embeddable Python for Windows.
"""
import ctypes as c
import os
import subprocess
import time
from ctypes import wintypes as w

kernel = c.WinDLL("kernel32", use_last_error=True)
advapi = c.WinDLL("advapi32", use_last_error=True)
user = c.WinDLL("user32", use_last_error=True)

TOKEN_DUPLICATE, TOKEN_QUERY, TOKEN_ASSIGN_PRIMARY, TOKEN_ADJUST_DEFAULT = 0x2, 0x8, 0x1, 0x80
SECURITY_IMPERSONATION, TOKEN_PRIMARY, TOKEN_INTEGRITY_LEVEL = 2, 1, 25
SE_GROUP_INTEGRITY = 0x20
CREATE_UNICODE_ENVIRONMENT, CREATE_NEW_PROCESS_GROUP = 0x400, 0x200
GENERIC_ALL = 0x10000000
INFINITE, WAIT_TIMEOUT, STILL_ACTIVE = 0xFFFFFFFF, 0x102, 259
SE_WINDOW_OBJECT, LABEL_SECURITY_INFORMATION = 7, 0x10


class STARTUPINFO(c.Structure):
    _fields_ = [("cb", w.DWORD), ("lpReserved", w.LPWSTR), ("lpDesktop", w.LPWSTR), ("lpTitle", w.LPWSTR),
                ("dwX", w.DWORD), ("dwY", w.DWORD), ("dwXSize", w.DWORD), ("dwYSize", w.DWORD),
                ("dwXCountChars", w.DWORD), ("dwYCountChars", w.DWORD), ("dwFillAttribute", w.DWORD),
                ("dwFlags", w.DWORD), ("wShowWindow", w.WORD), ("cbReserved2", w.WORD), ("lpReserved2", c.c_void_p),
                ("hStdInput", w.HANDLE), ("hStdOutput", w.HANDLE), ("hStdError", w.HANDLE)]


class PROCESS_INFORMATION(c.Structure):
    _fields_ = [("hProcess", w.HANDLE), ("hThread", w.HANDLE), ("dwProcessId", w.DWORD), ("dwThreadId", w.DWORD)]


class SID_AND_ATTRIBUTES(c.Structure):
    _fields_ = [("Sid", c.c_void_p), ("Attributes", w.DWORD)]


class TOKEN_MANDATORY_LABEL(c.Structure):
    _fields_ = [("Label", SID_AND_ATTRIBUTES)]


kernel.GetCurrentProcess.restype = w.HANDLE
kernel.CloseHandle.argtypes = [w.HANDLE]
kernel.WaitForSingleObject.argtypes = [w.HANDLE, w.DWORD]
kernel.WaitForSingleObject.restype = w.DWORD
kernel.GetExitCodeProcess.argtypes = [w.HANDLE, c.POINTER(w.DWORD)]
kernel.TerminateProcess.argtypes = [w.HANDLE, w.UINT]
kernel.LocalFree.argtypes = [c.c_void_p]
advapi.OpenProcessToken.argtypes = [w.HANDLE, w.DWORD, c.POINTER(w.HANDLE)]
advapi.DuplicateTokenEx.argtypes = [w.HANDLE, w.DWORD, c.c_void_p, c.c_int, c.c_int, c.POINTER(w.HANDLE)]
advapi.ConvertStringSidToSidW.argtypes = [w.LPCWSTR, c.POINTER(c.c_void_p)]
advapi.GetLengthSid.argtypes = [c.c_void_p]
advapi.GetLengthSid.restype = w.DWORD
advapi.SetTokenInformation.argtypes = [w.HANDLE, c.c_int, c.c_void_p, w.DWORD]
advapi.CreateProcessAsUserW.argtypes = [w.HANDLE, w.LPCWSTR, w.LPWSTR, c.c_void_p, c.c_void_p, w.BOOL, w.DWORD, c.c_void_p,
                                        w.LPCWSTR, c.POINTER(STARTUPINFO), c.POINTER(PROCESS_INFORMATION)]
advapi.ConvertStringSecurityDescriptorToSecurityDescriptorW.argtypes = [w.LPCWSTR, w.DWORD, c.POINTER(c.c_void_p), c.c_void_p]
advapi.GetSecurityDescriptorSacl.argtypes = [c.c_void_p, c.POINTER(w.BOOL), c.POINTER(c.c_void_p), c.POINTER(w.BOOL)]
advapi.SetSecurityInfo.argtypes = [w.HANDLE, c.c_int, w.DWORD, c.c_void_p, c.c_void_p, c.c_void_p, c.c_void_p]
advapi.SetSecurityInfo.restype = w.DWORD
user.CreateDesktopW.argtypes = [w.LPCWSTR, w.LPCWSTR, c.c_void_p, w.DWORD, w.DWORD, c.c_void_p]
user.CreateDesktopW.restype = w.HANDLE
user.CloseDesktop.argtypes = [w.HANDLE]


def _check(ok, what):
    if not ok:
        raise OSError(c.get_last_error(), what)


def label_low(folder):
    """The run folder (and what is made in it) writable by a low-integrity process."""
    subprocess.run(["icacls", str(folder), "/setintegritylevel", "(OI)(CI)low"], check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def _low_token():
    own = w.HANDLE()
    _check(advapi.OpenProcessToken(kernel.GetCurrentProcess(), TOKEN_DUPLICATE | TOKEN_QUERY | TOKEN_ASSIGN_PRIMARY
                                   | TOKEN_ADJUST_DEFAULT, c.byref(own)), "OpenProcessToken")
    low = w.HANDLE()
    _check(advapi.DuplicateTokenEx(own, 0, None, SECURITY_IMPERSONATION, TOKEN_PRIMARY, c.byref(low)), "DuplicateTokenEx")
    kernel.CloseHandle(own)
    sid = c.c_void_p()
    _check(advapi.ConvertStringSidToSidW("S-1-16-4096", c.byref(sid)), "ConvertStringSidToSid")  # Low mandatory level
    label = TOKEN_MANDATORY_LABEL(SID_AND_ATTRIBUTES(sid, SE_GROUP_INTEGRITY))
    _check(advapi.SetTokenInformation(low, TOKEN_INTEGRITY_LEVEL, c.byref(label), c.sizeof(label) + advapi.GetLengthSid(sid)),
           "SetTokenInformation")
    kernel.LocalFree(sid)
    return low


def _hidden_desktop(name):
    desk = user.CreateDesktopW(name, None, None, 0, GENERIC_ALL, None)
    _check(desk, "CreateDesktop")
    # a low-integrity process may make windows on it (its label: low, no-write-up from below otherwise)
    sd = c.c_void_p()
    _check(advapi.ConvertStringSecurityDescriptorToSecurityDescriptorW("S:(ML;;NW;;;LW)", 1, c.byref(sd), None), "label SD")
    present, defaulted, sacl = w.BOOL(), w.BOOL(), c.c_void_p()
    _check(advapi.GetSecurityDescriptorSacl(sd, c.byref(present), c.byref(sacl), c.byref(defaulted)), "label SACL")
    err = advapi.SetSecurityInfo(desk, SE_WINDOW_OBJECT, LABEL_SECURITY_INFORMATION, None, None, None, sacl)
    kernel.LocalFree(sd)
    if err != 0:
        raise OSError(err, "SetSecurityInfo (desktop label)")
    return desk


class JOBOBJECT_BASIC_LIMIT_INFORMATION(c.Structure):
    _fields_ = [("PerProcessUserTimeLimit", c.c_int64), ("PerJobUserTimeLimit", c.c_int64), ("LimitFlags", w.DWORD),
                ("MinimumWorkingSetSize", c.c_size_t), ("MaximumWorkingSetSize", c.c_size_t), ("ActiveProcessLimit", w.DWORD),
                ("Affinity", c.c_size_t), ("PriorityClass", w.DWORD), ("SchedulingClass", w.DWORD)]


class JOBOBJECT_EXTENDED_LIMIT_INFORMATION(c.Structure):
    _fields_ = [("BasicLimitInformation", JOBOBJECT_BASIC_LIMIT_INFORMATION), ("IoInfo", c.c_uint64 * 6),
                ("ProcessMemoryLimit", c.c_size_t), ("JobMemoryLimit", c.c_size_t), ("PeakProcessMemoryUsed", c.c_size_t),
                ("PeakJobMemoryUsed", c.c_size_t)]


kernel.CreateJobObjectW.restype = w.HANDLE
kernel.CreateJobObjectW.argtypes = [c.c_void_p, w.LPCWSTR]
kernel.SetInformationJobObject.argtypes = [w.HANDLE, c.c_int, c.c_void_p, w.DWORD]
kernel.AssignProcessToJobObject.argtypes = [w.HANDLE, w.HANDLE]
kernel.TerminateJobObject.argtypes = [w.HANDLE, w.UINT]
kernel.ResumeThread.argtypes = [w.HANDLE]
JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE, JOB_OBJECT_LIMIT_JOB_MEMORY, CREATE_SUSPENDED = 0x2000, 0x200, 0x4


def _job(memory_limit):
    """A job its processes cannot outlive (the run's scan children, sandboxed plugins, the indexer), with a memory cap
    for all of them together (the machine is shared)."""
    job = kernel.CreateJobObjectW(None, None)
    _check(job, "CreateJobObject")
    info = JOBOBJECT_EXTENDED_LIMIT_INFORMATION()
    info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_JOB_MEMORY
    info.JobMemoryLimit = memory_limit
    _check(kernel.SetInformationJobObject(job, 9, c.byref(info), c.sizeof(info)), "SetInformationJobObject")
    return job


class Process:
    def __init__(self, info, desktop, job):
        self.handle, self.pid, self.desktop, self.job = info.hProcess, info.dwProcessId, desktop, job
        _check(kernel.AssignProcessToJobObject(job, info.hProcess), "AssignProcessToJobObject")
        kernel.ResumeThread(info.hThread)
        kernel.CloseHandle(info.hThread)
        self.returncode = None

    def poll(self):
        if self.returncode is None and kernel.WaitForSingleObject(self.handle, 0) != WAIT_TIMEOUT:
            code = w.DWORD()
            kernel.GetExitCodeProcess(self.handle, c.byref(code))
            self.returncode = code.value
        return self.returncode

    def wait(self, timeout=None):
        end = None if timeout is None else time.time() + timeout
        while self.poll() is None:
            if end is not None and time.time() > end:
                raise subprocess.TimeoutExpired("LYNX", timeout)
            kernel.WaitForSingleObject(self.handle, 200)
        return self.returncode

    def kill(self):
        if self.poll() is None:
            kernel.TerminateProcess(self.handle, 0xDEAD)
        try:
            self.wait(30)
        except subprocess.TimeoutExpired:
            pass
        kernel.TerminateJobObject(self.job, 0xDEAD)  # (and every process it started)

    terminate = kill

    def close(self):
        if self.job:
            kernel.TerminateJobObject(self.job, 0xDEAD)  # (what the run left running: scans, the indexer)
            kernel.CloseHandle(self.job)
            self.job = None
        if self.desktop:
            user.CloseDesktop(self.desktop)
            self.desktop = None


def start_sandboxed(args, env, cwd, desktop, stdout_path, memory_limit=6 << 30):
    """args[0] the exe. The environment block replaces the caller's; stdout/stderr go to stdout_path."""
    desk = _hidden_desktop(desktop)
    token = _low_token()
    si = STARTUPINFO()
    si.cb = c.sizeof(si)
    si.lpDesktop = "WinSta0\\" + desktop
    # (stdout to a file the low process can write: inherited handle)
    import msvcrt
    out = open(stdout_path, "ab")
    h = msvcrt.get_osfhandle(out.fileno())
    os.set_handle_inheritable(h, True)
    si.dwFlags = 0x100  # STARTF_USESTDHANDLES
    si.hStdOutput = si.hStdError = h
    block = "".join(f"{k}={v}\0" for k, v in sorted(env.items())) + "\0"
    info = PROCESS_INFORMATION()
    cmd = subprocess.list2cmdline(args)
    _check(advapi.CreateProcessAsUserW(token, None, c.create_unicode_buffer(cmd), None, None, True,
                                       CREATE_UNICODE_ENVIRONMENT | CREATE_NEW_PROCESS_GROUP | CREATE_SUSPENDED, (c.c_wchar * len(block)) (*block),
                                       str(cwd), c.byref(si), c.byref(info)), "CreateProcessAsUser")
    kernel.CloseHandle(token)
    out.close()
    return Process(info, desk, _job(memory_limit))
