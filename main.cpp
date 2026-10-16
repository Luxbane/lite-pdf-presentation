#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0601

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <map>
#include <algorithm>

#include "include/fpdfview.h"

struct MonitorInfoItem {
    HMONITOR hMon;
    RECT rc;
    std::wstring name;
    bool isPrimary;
};

struct CachedPage {
    HBITMAP hBitmap;
    int width;
    int height;
};

static HINSTANCE g_hInstance = NULL;
static HWND g_hPresenterWnd = NULL;
static HWND g_hAudienceWnd = NULL;
static HFONT g_hFontUI = NULL;
static HFONT g_hFontTimer = NULL;
static HFONT g_hFontTitle = NULL;

static HWND g_hBtnOpen = NULL;
static HWND g_hLblFile = NULL;
static HWND g_hComboDisplay = NULL;
static HWND g_hBtnRefresh = NULL;
static HWND g_hBtnStart = NULL;
static HWND g_hBtnPrev = NULL;
static HWND g_hBtnNext = NULL;
static HWND g_hLblPage = NULL;
static HWND g_hBtnBlank = NULL;

static FPDF_DOCUMENT g_pdfDoc = NULL;
static int g_pageCount = 0;
static int g_currentPage = 0;
static bool g_isPresenting = false;
static bool g_isBlank = false;
static bool g_isEndBlack = false;
static UINT_PTR g_timerId = 0;
static int g_elapsedSeconds = 0;
static std::wstring g_currentFilePath;

static std::vector<MonitorInfoItem> g_monitors;
static std::map<int, CachedPage> g_pageCache;

static RECT g_rcCurrentSlide = {0};
static RECT g_rcNextSlide = {0};
static RECT g_rcTimerBox = {0};

static const COLORREF COLOR_BG = RGB(24, 24, 27);
static const COLORREF COLOR_CARD = RGB(39, 39, 42);
static const COLORREF COLOR_CARD_INNER = RGB(18, 18, 18);
static const COLORREF COLOR_BORDER = RGB(63, 63, 70);
static const COLORREF COLOR_TEXT = RGB(228, 228, 231);
static const COLORREF COLOR_TEXT_MUTED = RGB(161, 161, 170);
static const COLORREF COLOR_ACCENT_GREEN = RGB(76, 175, 80);
static const COLORREF COLOR_ACCENT_BLUE = RGB(33, 150, 243);
static const COLORREF COLOR_TIMER_YELLOW = RGB(255, 235, 59);

static HBRUSH g_hbrBg = NULL;
static HBRUSH g_hbrCard = NULL;
static HBRUSH g_hbrCardInner = NULL;

LRESULT CALLBACK PresenterWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK AudienceWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
void UpdateLayout(int width, int height);
void PopulateMonitors();
void LoadPDF(const std::wstring& filePath);
void RenderViews();
void StartPresentation();
void StopPresentation();
void NextSlide();
void PrevSlide();
void ToggleBlank();
void HandleKey(WPARAM key);
CachedPage GetPageBitmap(int pageIdx, int targetW, int targetH);
void ClearCache();

BOOL CALLBACK MonitorEnumProc(HMONITOR hMon, HDC, LPRECT lpRect, LPARAM) {
    MONITORINFOEXW mi;
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(hMon, &mi)) {
        MonitorInfoItem item;
        item.hMon = hMon;
        item.rc = mi.rcMonitor;
        item.isPrimary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
        int w = mi.rcMonitor.right - mi.rcMonitor.left;
        int h = mi.rcMonitor.bottom - mi.rcMonitor.top;
        int idx = (int)g_monitors.size() + 1;
        item.name = L"Display " + std::to_wstring(idx);
        if (item.isPrimary) {
            item.name += L" (Primary) - " + std::to_wstring(w) + L"x" + std::to_wstring(h);
        } else {
            item.name += L" (External / Projector) - " + std::to_wstring(w) + L"x" + std::to_wstring(h);
        }
        g_monitors.push_back(item);
    }
    return TRUE;
}

void PopulateMonitors() {
    g_monitors.clear();
    EnumDisplayMonitors(NULL, NULL, MonitorEnumProc, 0);

    if (g_hComboDisplay) {
        SendMessageW(g_hComboDisplay, CB_RESETCONTENT, 0, 0);
        int defaultSelect = 0;
        for (size_t i = 0; i < g_monitors.size(); ++i) {
            SendMessageW(g_hComboDisplay, CB_ADDSTRING, 0, (LPARAM)g_monitors[i].name.c_str());
            if (!g_monitors[i].isPrimary && defaultSelect == 0) {
                defaultSelect = (int)i;
            }
        }
        SendMessageW(g_hComboDisplay, CB_SETCURSEL, defaultSelect, 0);
    }
}

void ClearCache() {
    for (auto& pair : g_pageCache) {
        if (pair.second.hBitmap) {
            DeleteObject(pair.second.hBitmap);
        }
    }
    g_pageCache.clear();
}

CachedPage GetPageBitmap(int pageIdx, int targetW, int targetH) {
    if (!g_pdfDoc || pageIdx < 0 || pageIdx >= g_pageCount) {
        return {NULL, 0, 0};
    }

    auto it = g_pageCache.find(pageIdx);
    if (it != g_pageCache.end()) {
        return it->second;
    }

    FPDF_PAGE page = FPDF_LoadPage(g_pdfDoc, pageIdx);
    if (!page) {
        return {NULL, 0, 0};
    }

    double origW = FPDF_GetPageWidth(page);
    double origH = FPDF_GetPageHeight(page);
    if (origW <= 0 || origH <= 0) {
        FPDF_ClosePage(page);
        return {NULL, 0, 0};
    }

    int maxDim = (std::max)(targetW, targetH);
    if (maxDim < 1920) maxDim = 1920;
    maxDim *= 2;

    double scaleX = (double)maxDim / origW;
    double scaleY = (double)maxDim / origH;
    double scale = (std::min)(scaleX, scaleY);
    int finalW = (int)(origW * scale);
    int finalH = (int)(origH * scale);
    if (finalW < 1) finalW = 1;
    if (finalH < 1) finalH = 1;

    FPDF_BITMAP bitmap = FPDFBitmap_Create(finalW, finalH, 0);
    FPDFBitmap_FillRect(bitmap, 0, 0, finalW, finalH, 0xFFFFFFFF);
    FPDF_RenderPageBitmap(bitmap, page, 0, 0, finalW, finalH, 0, FPDF_ANNOT | FPDF_REVERSE_BYTE_ORDER);

    BITMAPINFO bmi;
    ZeroMemory(&bmi, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = finalW;
    bmi.bmiHeader.biHeight = -finalH;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pDIBBits = NULL;
    HDC hdc = GetDC(NULL);
    HBITMAP hBmp = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &pDIBBits, NULL, 0);
    ReleaseDC(NULL, hdc);

    if (hBmp && pDIBBits) {
        const void* src = FPDFBitmap_GetBuffer(bitmap);
        int stride = FPDFBitmap_GetStride(bitmap);
        if (stride == finalW * 4) {
            memcpy(pDIBBits, src, finalW * finalH * 4);
        } else {
            for (int y = 0; y < finalH; ++y) {
                memcpy((BYTE*)pDIBBits + y * finalW * 4, (const BYTE*)src + y * stride, finalW * 4);
            }
        }
    }

    FPDFBitmap_Destroy(bitmap);
    FPDF_ClosePage(page);

    CachedPage cp = {hBmp, finalW, finalH};
    g_pageCache[pageIdx] = cp;
    return cp;
}

void LoadPDF(const std::wstring& filePath) {
    if (g_isPresenting) {
        StopPresentation();
    }
    ClearCache();
    if (g_pdfDoc) {
        FPDF_CloseDocument(g_pdfDoc);
        g_pdfDoc = NULL;
    }

    g_pdfDoc = FPDF_LoadDocument(std::string(filePath.begin(), filePath.end()).c_str(), NULL);
    if (!g_pdfDoc) {
        MessageBoxW(g_hPresenterWnd, L"Failed to open PDF document.", L"Error", MB_ICONERROR);
        return;
    }

    g_currentFilePath = filePath;
    g_pageCount = FPDF_GetPageCount(g_pdfDoc);
    g_currentPage = 0;
    g_isEndBlack = false;
    g_isBlank = false;

    size_t lastSlash = filePath.find_last_of(L"\\/");
    std::wstring fileName = (lastSlash != std::wstring::npos) ? filePath.substr(lastSlash + 1) : filePath;
    SetWindowTextW(g_hLblFile, fileName.c_str());

    EnableWindow(g_hBtnStart, TRUE);
    EnableWindow(g_hBtnPrev, TRUE);
    EnableWindow(g_hBtnNext, TRUE);
    EnableWindow(g_hBtnBlank, TRUE);

    RenderViews();
}

void StartPresentation() {
    if (!g_pdfDoc || g_isPresenting) return;

    int sel = (int)SendMessageW(g_hComboDisplay, CB_GETCURSEL, 0, 0);
    if (sel < 0 || sel >= (int)g_monitors.size()) {
        sel = 0;
    }

    RECT monRect = g_monitors[sel].rc;

    if (!g_hAudienceWnd) {
        g_hAudienceWnd = CreateWindowExW(
            WS_EX_TOPMOST,
            L"LitePDFAudienceClass",
            L"Lite PDF Presentation - Audience Display",
            WS_POPUP | WS_VISIBLE,
            monRect.left, monRect.top,
            monRect.right - monRect.left,
            monRect.bottom - monRect.top,
            NULL, NULL, g_hInstance, NULL
        );
    } else {
        SetWindowPos(g_hAudienceWnd, HWND_TOPMOST,
            monRect.left, monRect.top,
            monRect.right - monRect.left,
            monRect.bottom - monRect.top,
            SWP_SHOWWINDOW);
    }

    g_isPresenting = true;
    g_isEndBlack = false;
    SetWindowTextW(g_hBtnStart, L"\u23F9 Stop Presentation (Esc)");

    g_elapsedSeconds = 0;
    g_timerId = SetTimer(g_hPresenterWnd, 1, 1000, NULL);

    RenderViews();
}

void StopPresentation() {
    if (g_hAudienceWnd) {
        HWND hAud = g_hAudienceWnd;
        g_hAudienceWnd = NULL;
        DestroyWindow(hAud);
    }
    if (g_timerId) {
        KillTimer(g_hPresenterWnd, g_timerId);
        g_timerId = 0;
    }
    g_isPresenting = false;
    g_isEndBlack = false;
    SetWindowTextW(g_hBtnStart, L"\u25B6 Start Presentation (F5)");
    RenderViews();
}

void NextSlide() {
    if (!g_pdfDoc) return;

    if (g_isEndBlack) {
        StopPresentation();
        return;
    }

    if (g_currentPage < g_pageCount - 1) {
        g_currentPage++;
        if (g_isBlank) {
            g_isBlank = false;
            SetWindowTextW(g_hBtnBlank, L"\u25A0 Black Screen (B)");
        }
        RenderViews();
    } else if (g_currentPage == g_pageCount - 1) {
        if (g_isPresenting) {
            g_isEndBlack = true;
            RenderViews();
        }
    }
}

void PrevSlide() {
    if (!g_pdfDoc) return;

    if (g_isEndBlack) {
        g_isEndBlack = false;
        RenderViews();
        return;
    }

    if (g_currentPage > 0) {
        g_currentPage--;
        if (g_isBlank) {
            g_isBlank = false;
            SetWindowTextW(g_hBtnBlank, L"\u25A0 Black Screen (B)");
        }
        RenderViews();
    }
}

void ToggleBlank() {
    if (!g_pdfDoc || g_isEndBlack) return;
    g_isBlank = !g_isBlank;
    SetWindowTextW(g_hBtnBlank, g_isBlank ? L"\u25A1 Resume Slide (B)" : L"\u25A0 Black Screen (B)");
    RenderViews();
}

void HandleKey(WPARAM key) {
    switch (key) {
        case VK_F5:
            if (!g_isPresenting) StartPresentation();
            break;
        case VK_ESCAPE:
            if (g_isPresenting) StopPresentation();
            break;
        case VK_RIGHT:
        case VK_SPACE:
        case VK_NEXT:
            NextSlide();
            break;
        case VK_LEFT:
        case VK_PRIOR:
            PrevSlide();
            break;
        case 'B':
            ToggleBlank();
            break;
    }
}

void RenderViews() {
    if (g_pdfDoc) {
        std::wstring pageText = L"Slide: " + std::to_wstring(g_currentPage + 1) + L" / " + std::to_wstring(g_pageCount);
        if (g_isEndBlack) {
            pageText += L" (End)";
        }
        SetWindowTextW(g_hLblPage, pageText.c_str());
    }

    if (g_hPresenterWnd) {
        InvalidateRect(g_hPresenterWnd, NULL, FALSE);
    }
    if (g_hAudienceWnd) {
        InvalidateRect(g_hAudienceWnd, NULL, FALSE);
    }
}

void DrawCenteredBitmap(HDC hdc, HBITMAP hBmp, int bmpW, int bmpH, const RECT& targetRect) {
    if (!hBmp || bmpW <= 0 || bmpH <= 0) return;

    int targetW = targetRect.right - targetRect.left;
    int targetH = targetRect.bottom - targetRect.top;
    if (targetW <= 0 || targetH <= 0) return;

    double scaleX = (double)targetW / bmpW;
    double scaleY = (double)targetH / bmpH;
    double scale = (std::min)(scaleX, scaleY);
    int destW = (int)(bmpW * scale);
    int destH = (int)(bmpH * scale);
    int destX = targetRect.left + (targetW - destW) / 2;
    int destY = targetRect.top + (targetH - destH) / 2;

    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, hBmp);

    SetStretchBltMode(hdc, HALFTONE);
    SetBrushOrgEx(hdc, 0, 0, NULL);
    StretchBlt(hdc, destX, destY, destW, destH, memDC, 0, 0, bmpW, bmpH, SRCCOPY);

    SelectObject(memDC, oldBmp);
    DeleteDC(memDC);
}

void UpdateLayout(int width, int height) {
    int topH = 50;
    int bottomH = 55;
    int margin = 15;

    SetWindowPos(g_hBtnOpen, NULL, margin, 10, 110, 32, SWP_NOZORDER);
    SetWindowPos(g_hLblFile, NULL, margin + 120, 16, 250, 24, SWP_NOZORDER);
    SetWindowPos(g_hComboDisplay, NULL, width - 420, 12, 190, 200, SWP_NOZORDER);
    SetWindowPos(g_hBtnRefresh, NULL, width - 220, 10, 75, 32, SWP_NOZORDER);
    SetWindowPos(g_hBtnStart, NULL, width - 135, 10, 120, 32, SWP_NOZORDER);

    int botY = height - bottomH + 12;
    SetWindowPos(g_hBtnPrev, NULL, margin, botY, 110, 32, SWP_NOZORDER);
    SetWindowPos(g_hBtnNext, NULL, margin + 120, botY, 110, 32, SWP_NOZORDER);
    SetWindowPos(g_hLblPage, NULL, margin + 245, botY + 6, 180, 24, SWP_NOZORDER);
    SetWindowPos(g_hBtnBlank, NULL, width - 165, botY, 150, 32, SWP_NOZORDER);

    int contentY = topH + 8;
    int contentH = height - topH - bottomH - 8;
    int contentW = width - (margin * 2);

    int leftW = (contentW * 60) / 100;
    int rightW = contentW - leftW - 15;

    g_rcCurrentSlide.left = margin;
    g_rcCurrentSlide.top = contentY;
    g_rcCurrentSlide.right = margin + leftW;
    g_rcCurrentSlide.bottom = contentY + contentH;

    int rightX = margin + leftW + 15;
    int nextH = (contentH * 60) / 100;
    g_rcNextSlide.left = rightX;
    g_rcNextSlide.top = contentY;
    g_rcNextSlide.right = rightX + rightW;
    g_rcNextSlide.bottom = contentY + nextH;

    g_rcTimerBox.left = rightX;
    g_rcTimerBox.top = contentY + nextH + 12;
    g_rcTimerBox.right = rightX + rightW;
    g_rcTimerBox.bottom = contentY + contentH;
}

LRESULT CALLBACK AudienceWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_KEYDOWN:
            HandleKey(wParam);
            return 0;
        case WM_CLOSE:
            StopPresentation();
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            RECT rc;
            GetClientRect(hWnd, &rc);

            HDC memDC = CreateCompatibleDC(hdc);
            HBITMAP memBmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
            HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);

            HBRUSH hbrBlack = (HBRUSH)GetStockObject(BLACK_BRUSH);
            FillRect(memDC, &rc, hbrBlack);

            if (!g_isBlank && !g_isEndBlack && g_pdfDoc) {
                CachedPage cp = GetPageBitmap(g_currentPage, rc.right, rc.bottom);
                if (cp.hBitmap) {
                    DrawCenteredBitmap(memDC, cp.hBitmap, cp.width, cp.height, rc);
                }
            }

            BitBlt(hdc, 0, 0, rc.right, rc.bottom, memDC, 0, 0, SRCCOPY);
            SelectObject(memDC, oldBmp);
            DeleteObject(memBmp);
            DeleteDC(memDC);

            EndPaint(hWnd, &ps);
            return 0;
        }
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

LRESULT CALLBACK PresenterWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            g_hBtnOpen = CreateWindowW(L"BUTTON", L"Open PDF", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0, hWnd, (HMENU)101, g_hInstance, NULL);
            g_hLblFile = CreateWindowW(L"STATIC", L"No file selected", WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 0, 0, hWnd, (HMENU)102, g_hInstance, NULL);
            g_hComboDisplay = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 0, 0, 0, 0, hWnd, (HMENU)103, g_hInstance, NULL);
            g_hBtnRefresh = CreateWindowW(L"BUTTON", L"Refresh", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0, hWnd, (HMENU)104, g_hInstance, NULL);
            g_hBtnStart = CreateWindowW(L"BUTTON", L"\u25B6 Start Presentation (F5)", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_DISABLED, 0, 0, 0, 0, hWnd, (HMENU)105, g_hInstance, NULL);
            g_hBtnPrev = CreateWindowW(L"BUTTON", L"\u25C0 Previous", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_DISABLED, 0, 0, 0, 0, hWnd, (HMENU)106, g_hInstance, NULL);
            g_hBtnNext = CreateWindowW(L"BUTTON", L"Next \u25B6", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_DISABLED, 0, 0, 0, 0, hWnd, (HMENU)107, g_hInstance, NULL);
            g_hLblPage = CreateWindowW(L"STATIC", L"Slide: 0 / 0", WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 0, 0, hWnd, (HMENU)108, g_hInstance, NULL);
            g_hBtnBlank = CreateWindowW(L"BUTTON", L"\u25A0 Black Screen (B)", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_DISABLED, 0, 0, 0, 0, hWnd, (HMENU)109, g_hInstance, NULL);

            HWND controls[] = {g_hBtnOpen, g_hLblFile, g_hComboDisplay, g_hBtnRefresh, g_hBtnStart, g_hBtnPrev, g_hBtnNext, g_hLblPage, g_hBtnBlank};
            for (HWND hCtl : controls) {
                SendMessageW(hCtl, WM_SETFONT, (WPARAM)g_hFontUI, TRUE);
            }

            PopulateMonitors();
            DragAcceptFiles(hWnd, TRUE);
            return 0;
        }

        case WM_SIZE: {
            int w = LOWORD(lParam);
            int h = HIWORD(lParam);
            UpdateLayout(w, h);
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }

        case WM_COMMAND: {
            int wmId = LOWORD(wParam);
            switch (wmId) {
                case 101: {
                    OPENFILENAMEW ofn;
                    wchar_t szFile[MAX_PATH] = {0};
                    ZeroMemory(&ofn, sizeof(ofn));
                    ofn.lStructSize = sizeof(ofn);
                    ofn.hwndOwner = hWnd;
                    ofn.lpstrFilter = L"PDF Files (*.pdf)\0*.pdf\0All Files (*.*)\0*.*\0";
                    ofn.lpstrFile = szFile;
                    ofn.nMaxFile = MAX_PATH;
                    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
                    if (GetOpenFileNameW(&ofn)) {
                        LoadPDF(szFile);
                    }
                    SetFocus(hWnd);
                    break;
                }
                case 104:
                    PopulateMonitors();
                    SetFocus(hWnd);
                    break;
                case 105:
                    if (g_isPresenting) StopPresentation();
                    else StartPresentation();
                    SetFocus(hWnd);
                    break;
                case 106:
                    PrevSlide();
                    SetFocus(hWnd);
                    break;
                case 107:
                    NextSlide();
                    SetFocus(hWnd);
                    break;
                case 109:
                    ToggleBlank();
                    SetFocus(hWnd);
                    break;
            }
            return 0;
        }

        case WM_DROPFILES: {
            HDROP hDrop = (HDROP)wParam;
            wchar_t szFilePath[MAX_PATH];
            if (DragQueryFileW(hDrop, 0, szFilePath, MAX_PATH)) {
                std::wstring path(szFilePath);
                if (path.length() >= 4 && _wcsicmp(path.c_str() + path.length() - 4, L".pdf") == 0) {
                    LoadPDF(path);
                }
            }
            DragFinish(hDrop);
            return 0;
        }

        case WM_TIMER:
            if (wParam == 1) {
                g_elapsedSeconds++;
                InvalidateRect(hWnd, &g_rcTimerBox, FALSE);
            }
            return 0;

        case WM_KEYDOWN:
            HandleKey(wParam);
            return 0;

        case WM_CTLCOLORSTATIC: {
            HDC hdcStatic = (HDC)wParam;
            SetTextColor(hdcStatic, COLOR_TEXT);
            SetBkMode(hdcStatic, TRANSPARENT);
            return (LRESULT)g_hbrBg;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            RECT rcClient;
            GetClientRect(hWnd, &rcClient);

            HDC memDC = CreateCompatibleDC(hdc);
            HBITMAP memBmp = CreateCompatibleBitmap(hdc, rcClient.right, rcClient.bottom);
            HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);

            FillRect(memDC, &rcClient, g_hbrBg);

            auto DrawCard = [&](const RECT& cardRect, const wchar_t* title, COLORREF titleColor) {
                FillRect(memDC, &cardRect, g_hbrCard);
                FrameRect(memDC, &cardRect, (HBRUSH)GetStockObject(GRAY_BRUSH));

                RECT titleRect = cardRect;
                titleRect.left += 12;
                titleRect.top += 8;
                titleRect.bottom = titleRect.top + 22;

                SelectObject(memDC, g_hFontTitle);
                SetTextColor(memDC, titleColor);
                SetBkMode(memDC, TRANSPARENT);
                DrawTextW(memDC, title, -1, &titleRect, DT_SINGLELINE | DT_LEFT);

                RECT innerRect = cardRect;
                innerRect.left += 10;
                innerRect.top += 32;
                innerRect.right -= 10;
                innerRect.bottom -= 10;
                FillRect(memDC, &innerRect, g_hbrCardInner);
                return innerRect;
            };

            RECT innerCurrent = DrawCard(g_rcCurrentSlide, L"CURRENT SLIDE", COLOR_ACCENT_GREEN);
            if (g_isEndBlack) {
                SelectObject(memDC, g_hFontUI);
                SetTextColor(memDC, COLOR_TEXT_MUTED);
                SetBkMode(memDC, TRANSPARENT);
                DrawTextW(memDC, L"End of slide show.\nAdvance again to exit presentation.", -1, &innerCurrent, DT_CENTER | DT_VCENTER);
            } else if (g_pdfDoc) {
                CachedPage cp = GetPageBitmap(g_currentPage, innerCurrent.right - innerCurrent.left, innerCurrent.bottom - innerCurrent.top);
                if (cp.hBitmap) {
                    DrawCenteredBitmap(memDC, cp.hBitmap, cp.width, cp.height, innerCurrent);
                }
            } else {
                SelectObject(memDC, g_hFontUI);
                SetTextColor(memDC, COLOR_TEXT_MUTED);
                SetBkMode(memDC, TRANSPARENT);
                DrawTextW(memDC, L"Please open or drop a PDF file here", -1, &innerCurrent, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }

            RECT innerNext = DrawCard(g_rcNextSlide, L"NEXT SLIDE", COLOR_ACCENT_BLUE);
            if (g_isEndBlack) {
                SelectObject(memDC, g_hFontTitle);
                SetTextColor(memDC, RGB(239, 68, 68));
                SetBkMode(memDC, TRANSPARENT);
                DrawTextW(memDC, L"Exit Presentation", -1, &innerNext, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            } else if (g_pdfDoc) {
                if (g_currentPage + 1 < g_pageCount) {
                    CachedPage cp = GetPageBitmap(g_currentPage + 1, innerNext.right - innerNext.left, innerNext.bottom - innerNext.top);
                    if (cp.hBitmap) {
                        DrawCenteredBitmap(memDC, cp.hBitmap, cp.width, cp.height, innerNext);
                    }
                } else {
                    SelectObject(memDC, g_hFontUI);
                    SetTextColor(memDC, COLOR_TEXT_MUTED);
                    SetBkMode(memDC, TRANSPARENT);
                    DrawTextW(memDC, L"Black Screen (End)", -1, &innerNext, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                }
            } else {
                SelectObject(memDC, g_hFontUI);
                SetTextColor(memDC, COLOR_TEXT_MUTED);
                SetBkMode(memDC, TRANSPARENT);
                DrawTextW(memDC, L"-", -1, &innerNext, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }

            FillRect(memDC, &g_rcTimerBox, g_hbrCard);
            FrameRect(memDC, &g_rcTimerBox, (HBRUSH)GetStockObject(GRAY_BRUSH));

            RECT tHeadRect = g_rcTimerBox;
            tHeadRect.left += 12;
            tHeadRect.top += 8;
            tHeadRect.bottom = tHeadRect.top + 20;
            SelectObject(memDC, g_hFontUI);
            SetTextColor(memDC, COLOR_TEXT_MUTED);
            SetBkMode(memDC, TRANSPARENT);
            DrawTextW(memDC, L"ELAPSED TIME", -1, &tHeadRect, DT_SINGLELINE | DT_LEFT);

            int hours = g_elapsedSeconds / 3600;
            int mins = (g_elapsedSeconds % 3600) / 60;
            int secs = g_elapsedSeconds % 60;
            wchar_t szTimer[32];
            swprintf_s(szTimer, L"%02d:%02d:%02d", hours, mins, secs);

            RECT tValRect = g_rcTimerBox;
            tValRect.top += 30;
            SelectObject(memDC, g_hFontTimer);
            SetTextColor(memDC, COLOR_TIMER_YELLOW);
            SetBkMode(memDC, TRANSPARENT);
            DrawTextW(memDC, szTimer, -1, &tValRect, DT_CENTER | DT_SINGLELINE);

            BitBlt(hdc, 0, 0, rcClient.right, rcClient.bottom, memDC, 0, 0, SRCCOPY);
            SelectObject(memDC, oldBmp);
            DeleteObject(memBmp);
            DeleteDC(memDC);

            EndPaint(hWnd, &ps);
            return 0;
        }

        case WM_CLOSE:
            StopPresentation();
            DestroyWindow(hWnd);
            return 0;

        case WM_DESTROY:
            ClearCache();
            if (g_pdfDoc) {
                FPDF_CloseDocument(g_pdfDoc);
                g_pdfDoc = NULL;
            }
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR pCmdLine, int nCmdShow) {
    g_hInstance = hInstance;

    FPDF_LIBRARY_CONFIG config;
    config.version = 2;
    config.m_pUserFontPaths = NULL;
    config.m_pIsolate = NULL;
    config.m_v8EmbedderSlot = 0;
    FPDF_InitLibraryWithConfig(&config);

    InitCommonControls();

    g_hFontUI = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_hFontTitle = CreateFontW(-14, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_hFontTimer = CreateFontW(-28, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    g_hbrBg = CreateSolidBrush(COLOR_BG);
    g_hbrCard = CreateSolidBrush(COLOR_CARD);
    g_hbrCardInner = CreateSolidBrush(COLOR_CARD_INNER);

    WNDCLASSEXW wcAudience = {0};
    wcAudience.cbSize = sizeof(wcAudience);
    wcAudience.style = CS_HREDRAW | CS_VREDRAW;
    wcAudience.lpfnWndProc = AudienceWndProc;
    wcAudience.hInstance = hInstance;
    wcAudience.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wcAudience.lpszClassName = L"LitePDFAudienceClass";
    wcAudience.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(1));
    RegisterClassExW(&wcAudience);

    WNDCLASSEXW wcPresenter = {0};
    wcPresenter.cbSize = sizeof(wcPresenter);
    wcPresenter.style = CS_HREDRAW | CS_VREDRAW;
    wcPresenter.lpfnWndProc = PresenterWndProc;
    wcPresenter.hInstance = hInstance;
    wcPresenter.hbrBackground = g_hbrBg;
    wcPresenter.lpszClassName = L"LitePDFPresenterClass";
    wcPresenter.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(1));
    RegisterClassExW(&wcPresenter);

    g_hPresenterWnd = CreateWindowExW(
        0,
        L"LitePDFPresenterClass",
        L"Lite PDF Presentation (Native C++)",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1050, 700,
        NULL, NULL, hInstance, NULL
    );

    if (!g_hPresenterWnd) {
        FPDF_DestroyLibrary();
        return 0;
    }

    ShowWindow(g_hPresenterWnd, nCmdShow);
    UpdateWindow(g_hPresenterWnd);

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv && argc > 1) {
        std::wstring initialFile = argv[1];
        if (initialFile.length() >= 4 && _wcsicmp(initialFile.c_str() + initialFile.length() - 4, L".pdf") == 0) {
            LoadPDF(initialFile);
        }
        LocalFree(argv);
    }

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_hFontUI) DeleteObject(g_hFontUI);
    if (g_hFontTitle) DeleteObject(g_hFontTitle);
    if (g_hFontTimer) DeleteObject(g_hFontTimer);
    if (g_hbrBg) DeleteObject(g_hbrBg);
    if (g_hbrCard) DeleteObject(g_hbrCard);
    if (g_hbrCardInner) DeleteObject(g_hbrCardInner);

    FPDF_DestroyLibrary();
    return (int)msg.wParam;
}
