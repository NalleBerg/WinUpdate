// Minimal Win32 test program to verify ListView header text behavior
#include <windows.h>
#include <commctrl.h>
#include <string>
#include <fstream>

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_LISTVIEW_CLASSES };
    InitCommonControlsEx(&icc);

    const wchar_t cls[] = L"HeaderTestClass";
    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = hInstance;
    wc.lpszClassName = cls;
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(0, cls, L"Header Test", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 500, 200, NULL, NULL, hInstance, NULL);
    if (!hwnd) return 1;

    HWND hList = CreateWindowExW(0, WC_LISTVIEWW, NULL, WS_CHILD | WS_VISIBLE | LVS_REPORT,
        10, 10, 460, 120, hwnd, NULL, hInstance, NULL);
    ListView_SetExtendedListViewStyle(hList, LVS_EX_FULLROWSELECT);

    // persistent strings
    std::wstring headers[4] = { L"Package", L"Installed", L"Available", L"Skip" };

    LVCOLUMNW col{};
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
    col.cx = 260; col.fmt = LVCFMT_LEFT; col.pszText = (LPWSTR)headers[0].c_str(); ListView_InsertColumn(hList, 0, &col);
    col.cx = 80; col.fmt = LVCFMT_LEFT; col.pszText = (LPWSTR)headers[1].c_str(); ListView_InsertColumn(hList, 1, &col);
    col.cx = 80; col.fmt = LVCFMT_RIGHT; col.pszText = (LPWSTR)headers[2].c_str(); ListView_InsertColumn(hList, 2, &col);
    col.cx = 40; col.fmt = LVCFMT_CENTER; col.pszText = (LPWSTR)headers[3].c_str(); ListView_InsertColumn(hList, 3, &col);

    // Force header HDM_SETITEMW and then HDM_GETITEMW
    HWND hHeader = ListView_GetHeader(hList);
    if (hHeader) {
        for (int i = 0; i < 4; ++i) {
            HDITEMW hi{};
            hi.mask = HDI_FORMAT | HDI_TEXT;
            if (i == 0) hi.fmt = HDF_LEFT;
            else if (i == 1) hi.fmt = HDF_LEFT;
            else if (i == 2) hi.fmt = HDF_RIGHT;
            else hi.fmt = HDF_CENTER;
            hi.fmt |= HDF_STRING;
            hi.pszText = (LPWSTR)headers[i].c_str();
            SendMessageW(hHeader, HDM_SETITEMW, i, (LPARAM)&hi);
        }
    }

    // Allow the window to appear and paint
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    // Wait briefly then read back header items and write to logs
    Sleep(400);
    std::ofstream ofs("logs/header_test_output.txt", std::ios::binary | std::ios::trunc);
    if (ofs) {
        if (hHeader) {
            ofs << "HDM_GETITEMW results:\n";
            for (int i = 0; i < 4; ++i) {
                wchar_t buf[256] = {0};
                HDITEMW ghi{};
                ghi.mask = HDI_TEXT | HDI_FORMAT;
                ghi.pszText = buf; ghi.cchTextMax = (int)std::size(buf);
                BOOL ok = (BOOL)SendMessageW(hHeader, HDM_GETITEMW, i, (LPARAM)&ghi);
                // convert wide to utf8 simple
                int needed = WideCharToMultiByte(CP_UTF8, 0, buf, -1, NULL, 0, NULL, NULL);
                std::string txt(needed, '\0');
                if (needed) WideCharToMultiByte(CP_UTF8, 0, buf, -1, &txt[0], needed, NULL, NULL);
                ofs << i << ": OK=" << (ok?1:0) << " FMT=" << ghi.fmt << " TXT=" << txt.c_str() << "\n";
            }
        } else {
            ofs << "No header control available\n";
        }
        ofs.close();
    }

    // short loop to keep window visible briefly
    DWORD start = GetTickCount();
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        if (GetTickCount() - start > 1500) break;
    }
    DestroyWindow(hList);
    DestroyWindow(hwnd);
    return 0;
}
