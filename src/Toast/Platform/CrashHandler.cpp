///@file CrashHandler.cpp
///@author Dario
///@date 18/09/25

#ifdef _WIN32
#define UNICODE

#include "CrashHandler.hpp"

// clang-format off
#include <windows.h>
#include <dbghelp.h>
#include <csignal>
// clang-format on

#include <atomic>
#include <cstdio>
#include <string>
#include <vector>

// Link with Dbghelp.lib
#pragma comment(lib, "Dbghelp.lib")

// ---------- Config ----------
static constexpr int MAX_FRAMES = 64;
static constexpr int MAX_MSG_LEN = 4096;

// ---------- Shared crash buffer ----------
struct CrashInfo {
	DWORD crashedThreadId;
	DWORD exceptionCode;
	USHORT framesCount;
	void* frames[MAX_FRAMES];
	char message[MAX_MSG_LEN];
};

static CrashInfo g_crashInfo;
static CRITICAL_SECTION g_crashLock;
static std::atomic<DWORD> g_uiThreadId { 0 };

static HANDLE g_hUiReadyEvent = nullptr;       // set by UI thread when its message queue is ready
static HANDLE g_hUiFinishedEvent = nullptr;    // set by UI thread when user closes or dump created
static HWND g_hwndCrash = nullptr;

// UI resources
static HFONT g_hMonoFont = nullptr;
static std::vector<std::wstring> g_listItems;

// Forward
LONG WINAPI TopLevelExceptionFilter(EXCEPTION_POINTERS* pExceptionPointers);
DWORD WINAPI CrashUiThreadProc(LPVOID);

// Utility: initialize static data
static void InitCrashData() {
	InitializeCriticalSection(&g_crashLock);
	ZeroMemory(&g_crashInfo, sizeof(g_crashInfo));
}

// ---------- Public install / uninstall ----------
void InstallCrashHandler() {
	InitCrashData();

#ifndef NDEBUG
	// create events
	g_hUiReadyEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);    // manual-reset
	g_hUiFinishedEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

	// start crash UI thread
	HANDLE h = CreateThread(nullptr, 0, CrashUiThreadProc, nullptr, 0, nullptr);
	if (h) {
		CloseHandle(h);
	}
#endif

	// basic terminate handlers/signals
	std::set_terminate([]() {
		fprintf(stderr, "std::terminate called\n");
		abort();
	});

	// signal(SIGABRT,
	//        [](int) {
	//	       fprintf(stderr, "SIGABRT\n");
	//	       abort();
	//        });
	// signal(SIGSEGV,
	//        [](int) {
	//	       fprintf(stderr, "SIGSEGV\n");
	//	       abort();
	//        });
	// signal(SIGFPE,
	//        [](int) {
	//	       fprintf(stderr, "SIGFPE\n");
	//	       abort();
	//        });

	// install SEH filter
	SetUnhandledExceptionFilter(TopLevelExceptionFilter);
}

static void WriteMiniDumpToExeDir(EXCEPTION_POINTERS* pExceptionPointers) {
	// get exe path and directory
	wchar_t exePath[MAX_PATH] = { 0 };
	if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) == 0) {
		// fallback to temp
		exePath[0] = L'\0';
	}

	wchar_t dirPath[MAX_PATH] = { 0 };
	if (exePath[0]) {
		wcscpy_s(dirPath, exePath);
		wchar_t* lastSlash = wcsrchr(dirPath, L'\\');
		if (lastSlash) {
			*lastSlash = L'\0';
		}
	}

	// build timestamped filename
	SYSTEMTIME st;
	GetLocalTime(&st);
	DWORD pid = GetCurrentProcessId();

	wchar_t outPath[MAX_PATH] = { 0 };
	if (dirPath[0]) {
		swprintf_s(
		    outPath,
		    MAX_PATH,
		    L"%s\\crashdump_%04d%02d%02d_%02d%02d%02d_pid%u.dmp",
		    dirPath,
		    st.wYear,
		    st.wMonth,
		    st.wDay,
		    st.wHour,
		    st.wMinute,
		    st.wSecond,
		    pid
		);
	} else {
		// no exe dir, use temp
		outPath[0] = L'\0';
	}

	// Try create file in exe dir first
	HANDLE hFile = INVALID_HANDLE_VALUE;
	if (outPath[0]) {
		hFile = CreateFileW(outPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	}

	// If failed, fall back to TEMP
	if (hFile == INVALID_HANDLE_VALUE) {
		wchar_t tmpPath[MAX_PATH] = { 0 };
		if (GetTempPathW(MAX_PATH, tmpPath) && tmpPath[0]) {
			wchar_t tmpFile[MAX_PATH] = { 0 };
			if (GetTempFileNameW(tmpPath, L"crh", 0, tmpFile)) {
				// change extension to .dmp
				std::wstring s(tmpFile);
				size_t pos = s.find_last_of(L'.');
				if (pos != std::wstring::npos) {
					s = s.substr(0, pos) + L".dmp";
				}
				wcscpy_s(outPath, s.c_str());
				hFile = CreateFileW(outPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
			}
		}
	}

	if (hFile == INVALID_HANDLE_VALUE) {
		// nothing we can do here
		return;
	}

	// Write the minidump using the real exception pointers
	MINIDUMP_EXCEPTION_INFORMATION mdei;
	mdei.ThreadId = GetCurrentThreadId();
	mdei.ExceptionPointers = pExceptionPointers;
	mdei.ClientPointers = FALSE;

	MINIDUMP_TYPE dumpType = static_cast<MINIDUMP_TYPE>(
	    MiniDumpWithFullMemory | MiniDumpWithFullMemoryInfo | MiniDumpWithHandleData | MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules |
	    MiniDumpWithProcessThreadData
	);
	BOOL ok = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile, dumpType, &mdei, nullptr, nullptr);
	CloseHandle(hFile);

	if (ok) {
		OutputDebugStringW(outPath);
	}
}

// ---------- Exception filter ----------
LONG WINAPI TopLevelExceptionFilter(EXCEPTION_POINTERS* pExceptionPointers) {
	// Capture short backtrace (no allocations)
	USHORT captured = CaptureStackBackTrace(0, MAX_FRAMES, g_crashInfo.frames, nullptr);

	EnterCriticalSection(&g_crashLock);
	g_crashInfo.crashedThreadId = GetCurrentThreadId();
	g_crashInfo.exceptionCode = pExceptionPointers && pExceptionPointers->ExceptionRecord ? pExceptionPointers->ExceptionRecord->ExceptionCode : 0;
	g_crashInfo.framesCount = captured;
	snprintf(g_crashInfo.message, MAX_MSG_LEN, "Exception 0x%08X in thread %u", g_crashInfo.exceptionCode, g_crashInfo.crashedThreadId);
	LeaveCriticalSection(&g_crashLock);
	// RELEASE build:
#ifdef NDEBUG
	// WriteMiniDumpToExeDir(pExceptionPointers);
	return EXCEPTION_EXECUTE_HANDLER;
}
#else

	// DEBUG build:
	if (g_hUiReadyEvent) {
		WaitForSingleObject(g_hUiReadyEvent, 2000);
	}

	DWORD uiTid = g_uiThreadId.load();
	if (uiTid != 0) {
		PostThreadMessageW(uiTid, WM_APP + 1, 0, 0);
	}

	// Wait for UI finished;
	DWORD timeout = 300000;    // 5 minutes auto close

	if (g_hUiFinishedEvent) {
		WaitForSingleObject(g_hUiFinishedEvent, timeout);
	} else {
		Sleep(500);
	}

	return EXCEPTION_EXECUTE_HANDLER;
}

// ---------- Symbol resolving helper ----------
static std::wstring ResolveAddrToString(DWORD64 addr) {
	wchar_t tmpbuf[64];
	swprintf(tmpbuf, _countof(tmpbuf), L"0x%llX", static_cast<unsigned long long>(addr));
	std::wstring out = tmpbuf;

	HANDLE cur = GetCurrentProcess();
	SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME | SYMOPT_LOAD_LINES);

	BYTE symBuffer[sizeof(SYMBOL_INFO) + 1024];
	PSYMBOL_INFO pSym = reinterpret_cast<PSYMBOL_INFO>(symBuffer);
	pSym->SizeOfStruct = sizeof(SYMBOL_INFO);
	pSym->MaxNameLen = 1024;
	DWORD64 displacement = 0;
	if (SymFromAddr(cur, addr, &displacement, pSym)) {
		WCHAR namew[1024];
		MultiByteToWideChar(CP_ACP, 0, pSym->Name, -1, namew, _countof(namew));
		out += L"  ";
		out += namew;

		IMAGEHLP_LINE64 line = { 0 };
		line.SizeOfStruct = sizeof(line);
		DWORD dwDispLine = 0;
		if (SymGetLineFromAddr64(cur, addr, &dwDispLine, &line)) {
			WCHAR filew[MAX_PATH];
			MultiByteToWideChar(CP_ACP, 0, line.FileName, -1, filew, MAX_PATH);
			wchar_t loc[128];
			swprintf(loc, _countof(loc), L" (%s:%u)", filew, line.LineNumber);
			out += loc;
		}
	} else {
		out += L" (symbol-not-found)";
	}
	return out;
}

// ---------- UI IDs ----------
static constexpr int ID_LISTBACK = 2001;
static constexpr int ID_BTN_DUMP = 1001;
static constexpr int ID_BTN_CLOSE = 1002;
static constexpr int ID_EDIT_DETAIL = 3001;

// ---------- UI: populate the listbox ----------
void PopulateBacktraceList(HWND hList) {
	EnterCriticalSection(&g_crashLock);
	CrashInfo snapshot = g_crashInfo;    // shallow copy
	LeaveCriticalSection(&g_crashLock);

	// Keep strings alive in global vector for lifetime of UI
	g_listItems.clear();
	SendMessageW(hList, LB_RESETCONTENT, 0, 0);

	// prepare font/hdc measurement
	HDC hdc = GetDC(hList);
	HFONT old = nullptr;
	if (g_hMonoFont) {
		old = static_cast<HFONT>(SelectObject(hdc, g_hMonoFont));
	}

	int maxWidth = 0;
	for (USHORT i = 0; i < snapshot.framesCount; ++i) {
		DWORD64 addr = reinterpret_cast<uintptr_t>(snapshot.frames[i]);
		std::wstring line = ResolveAddrToString(addr);
		g_listItems.push_back(line);
		SendMessageW(hList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(g_listItems.back().c_str()));

		// measure width
		SIZE sz;
		if (GetTextExtentPoint32W(hdc, line.c_str(), static_cast<int>(line.length()), &sz)) {
			if (sz.cx > maxWidth) {
				maxWidth = sz.cx;
			}
		}
	}

	// set horizontal extent for scroll
	SendMessageW(hList, LB_SETHORIZONTALEXTENT, static_cast<WPARAM>(maxWidth + 40), 0);

	if (old) {
		SelectObject(hdc, old);
	}
	ReleaseDC(hList, hdc);

	// Clear detail edit
	if (HWND hEdit = (HWND)GetDlgItem(g_hwndCrash, ID_EDIT_DETAIL)) {
		SetWindowTextW(hEdit, L"");
	}
}

// ---------- UI: write minidump (returns true on success) ----------
bool CreateMinidumpDialogAndWrite(HWND hwndParent, std::wstring* outPath = nullptr) {
	// Save-as dialog
	wchar_t filename[MAX_PATH] = L"crash.dmp";
	OPENFILENAMEW ofn = {};
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = hwndParent;
	ofn.lpstrFile = filename;
	ofn.nMaxFile = MAX_PATH;
	ofn.lpstrFilter = L"Dump Files\0*.dmp\0All Files\0*.*\0";
	ofn.Flags = OFN_EXPLORER | OFN_OVERWRITEPROMPT;
	ofn.lpstrDefExt = L"dmp";

	BOOL gotName = GetSaveFileNameW(&ofn);
	if (!gotName) {
		DWORD err = CommDlgExtendedError();
		if (err != 0) {
			// try automatic fallback to TEMP
			wchar_t tmpPath[MAX_PATH];
			if (GetTempPathW(MAX_PATH, tmpPath) == 0) {
				return false;
			}

			wchar_t tmpFile[MAX_PATH];
			if (GetTempFileNameW(tmpPath, L"crh", 0, tmpFile) == 0) {
				return false;
			}
			std::wstring s(tmpFile);
			size_t pos = s.find_last_of(L'.');
			if (pos != std::wstring::npos) {
				s = s.substr(0, pos) + L".dmp";
			}
			wcscpy_s(filename, s.c_str());
		} else {
			// user cancelled
			return false;
		}
	}

	HANDLE hFile = CreateFileW(filename, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (hFile == INVALID_HANDLE_VALUE) {
		MessageBoxW(hwndParent, L"Failed to create dump file.", L"Error", MB_ICONERROR);
		return false;
	}

	MINIDUMP_EXCEPTION_INFORMATION mdei;
	mdei.ThreadId = g_crashInfo.crashedThreadId;
	mdei.ExceptionPointers = nullptr;
	mdei.ClientPointers = FALSE;

	BOOL ok = MiniDumpWriteDump(
	    GetCurrentProcess(),
	    GetCurrentProcessId(),
	    hFile,
	    (MINIDUMP_TYPE)(MiniDumpWithDataSegs | MiniDumpWithHandleData | MiniDumpWithThreadInfo),
	    nullptr,
	    nullptr,
	    nullptr
	);
	CloseHandle(hFile);

	if (!ok) {
		MessageBoxW(hwndParent, L"MiniDumpWriteDump failed.", L"Error", MB_ICONERROR);
		return false;
	}

	if (outPath) {
		*outPath = filename;
	}
	{
		wchar_t msg[512];
		swprintf_s(msg, L"Minidump written to: %s", filename);
		MessageBoxW(hwndParent, msg, L"Done", MB_ICONINFORMATION);
	}
	return true;
}

// ---------- UI window proc ----------
// WIN32 API nightmare
LRESULT CALLBACK CrashWndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) {
	switch (msg) {
		case WM_CREATE: {
			// create font
			g_hMonoFont = CreateFontW(
			    -14,
			    0,
			    0,
			    0,
			    FW_NORMAL,
			    FALSE,
			    FALSE,
			    FALSE,
			    DEFAULT_CHARSET,
			    OUT_DEFAULT_PRECIS,
			    CLIP_DEFAULT_PRECIS,
			    DEFAULT_QUALITY,
			    FIXED_PITCH | FF_MODERN,
			    L"Consolas"
			);
			if (!g_hMonoFont) {
				g_hMonoFont = CreateFontW(
				    -14,
				    0,
				    0,
				    0,
				    FW_NORMAL,
				    FALSE,
				    FALSE,
				    FALSE,
				    DEFAULT_CHARSET,
				    OUT_DEFAULT_PRECIS,
				    CLIP_DEFAULT_PRECIS,
				    DEFAULT_QUALITY,
				    FIXED_PITCH | FF_MODERN,
				    L"Courier New"
				);
			}

			// Static label
			CreateWindowW(L"STATIC", L"Oh No! Toast Engine crashed (╯‵□′)╯︵┻━┻", WS_CHILD | WS_VISIBLE, 10, 10, 1180, 20, hWnd, nullptr, nullptr, nullptr);

			// Listbox with horizontal scroll
			HWND hList = CreateWindowW(
			    L"LISTBOX",
			    nullptr,
			    WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | LBS_USETABSTOPS | LBS_NOTIFY,
			    10,
			    35,
			    1180,
			    420,
			    hWnd,
			    reinterpret_cast<HMENU>(ID_LISTBACK),
			    nullptr,
			    nullptr
			);
			if (g_hMonoFont) {
				SendMessageW(hList, WM_SETFONT, reinterpret_cast<WPARAM>(g_hMonoFont), TRUE);
			}

			// Detail edit control (multiline read-only)
			HWND hEdit = CreateWindowW(
			    L"EDIT",
			    nullptr,
			    WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_LEFT | ES_AUTOVSCROLL | ES_READONLY | WS_BORDER | WS_TABSTOP | ES_MULTILINE,
			    10,
			    465,
			    1180,
			    140,
			    hWnd,
			    reinterpret_cast<HMENU>(ID_EDIT_DETAIL),
			    nullptr,
			    nullptr
			);
			if (g_hMonoFont) {
				SendMessageW(hEdit, WM_SETFONT, reinterpret_cast<WPARAM>(g_hMonoFont), TRUE);
			}

			// Buttons
			CreateWindowW(
			    L"BUTTON", L"Create Minidump", WS_CHILD | WS_VISIBLE, 10, 615, 160, 30, hWnd, reinterpret_cast<HMENU>(ID_BTN_DUMP), nullptr, nullptr
			);
			CreateWindowW(L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE, 180, 615, 100, 30, hWnd, reinterpret_cast<HMENU>(ID_BTN_CLOSE), nullptr, nullptr);

			PopulateBacktraceList(hList);
			break;
		}
		case WM_COMMAND:
			if (LOWORD(wp) == ID_BTN_DUMP) {
				std::wstring outPath;
				bool wrote = CreateMinidumpDialogAndWrite(hWnd, &outPath);
				if (wrote) {
					if (g_hUiFinishedEvent) {
						SetEvent(g_hUiFinishedEvent);
					}
					int res = MessageBoxW(hWnd, L"Minidump written. Close the crash window now?", L"Done", MB_YESNO | MB_ICONQUESTION);
					if (res == IDYES) {
						PostQuitMessage(0);
					}
				} else {
					// keep UI open
				}
			} else if (LOWORD(wp) == ID_BTN_CLOSE) {
				if (g_hUiFinishedEvent) {
					SetEvent(g_hUiFinishedEvent);
				}
				PostQuitMessage(0);
			} else if (LOWORD(wp) == ID_LISTBACK && HIWORD(wp) == LBN_SELCHANGE) {
				HWND hList = GetDlgItem(hWnd, ID_LISTBACK);
				int sel = int(SendMessageW(hList, LB_GETCURSEL, 0, 0));
				if (sel != LB_ERR) {
					// get text length
					int len = int(SendMessageW(hList, LB_GETTEXTLEN, static_cast<WPARAM>(sel), 0));
					if (len < 0) {
						len = 0;
					}
					// allocate buffer (len is count of TCHARs not including terminating null)
					std::wstring buf;
					buf.resize(len + 1);
					SendMessageW(hList, LB_GETTEXT, static_cast<WPARAM>(sel), reinterpret_cast<LPARAM>(buf.data()));
					// ensure null-termination (SendMessageW should write a null, but be safe)
					buf.resize(wcslen(buf.c_str()));
					HWND hEdit = GetDlgItem(hWnd, ID_EDIT_DETAIL);
					SetWindowTextW(hEdit, buf.c_str());
				}
			}
			break;
		case WM_DESTROY:
			if (g_hUiFinishedEvent) {
				SetEvent(g_hUiFinishedEvent);
			}
			// clean font
			if (g_hMonoFont) {
				DeleteObject(g_hMonoFont);
				g_hMonoFont = nullptr;
			}
			PostQuitMessage(0);
			break;
		default: return DefWindowProcW(hWnd, msg, wp, lp);
	}
	return 0;
}

// ---------- UI thread ----------
DWORD WINAPI CrashUiThreadProc(LPVOID) {
	// register window class
	WNDCLASSW wc = {};
	wc.lpfnWndProc = CrashWndProc;
	wc.hInstance = GetModuleHandle(nullptr);
	wc.lpszClassName = L"SingleProcessCrashReporterClass";
	RegisterClassW(&wc);

	// store thread id
	g_uiThreadId.store(GetCurrentThreadId());

	// Force message queue creation
	MSG pm;
	PeekMessageW(&pm, nullptr, 0, 0, PM_NOREMOVE);

	// signal readiness
	if (g_hUiReadyEvent) {
		SetEvent(g_hUiReadyEvent);
	}

	// initialize symbols for this (reporter) process.
	SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME | SYMOPT_LOAD_LINES);
	SymInitialize(GetCurrentProcess(), nullptr, TRUE);

	// Create the (hidden) window -- create visible now so debug users see it quickly
	g_hwndCrash = CreateWindowExW(
	    0,
	    L"SingleProcessCrashReporterClass",
	    L"Toast Crash Reporter",
	    WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX),
	    CW_USEDEFAULT,
	    CW_USEDEFAULT,
	    1200,
	    700,
	    nullptr,
	    nullptr,
	    GetModuleHandle(nullptr),
	    nullptr
	);
	if (!g_hwndCrash) {
		if (g_hUiFinishedEvent) {
			SetEvent(g_hUiFinishedEvent);
		}
		SymCleanup(GetCurrentProcess());
		return 0;
	}

	// Message loop - we will show the window when receiving WM_APP+1
	MSG msg;
	while (GetMessageW(&msg, nullptr, 0, 0)) {
		if (msg.message == WM_APP + 1) {
			// Show window and populate latest list
			ShowWindow(g_hwndCrash, SW_SHOW);
			UpdateWindow(g_hwndCrash);
			HWND hList = GetDlgItem(g_hwndCrash, ID_LISTBACK);
			if (hList) {
				PopulateBacktraceList(hList);
			}
			continue;
		}
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}

	// cleanup
	SymCleanup(GetCurrentProcess());
	if (g_hUiFinishedEvent) {
		SetEvent(g_hUiFinishedEvent);
	}
	return 0;
}
#endif    // NDEBUG

#endif    // _WIN32
