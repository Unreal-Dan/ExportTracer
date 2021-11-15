#include <Windows.h>
#include <Windowsx.h>
#include <uxtheme.h>
#include <inttypes.h>
#include <shlwapi.h>

#include <vector>
#include <string>

#include "Injector.h"

#pragma comment(lib, "Shlwapi.lib")

using namespace std;

// module base
EXTERN_C IMAGE_DOS_HEADER __ImageBase;
HINSTANCE imageBase = (HINSTANCE)&__ImageBase;

// background color brush 1
HBRUSH bkbrush;
// background color brush 2
HBRUSH bkbrush2;
// brush for down arrow in dropdown
HBRUSH arrowbrush;
// pen for drawing border on dropdown
HPEN borderpen;
// pen for drawing arrow in dropdown
HPEN arrowpen;

// background color 1
COLORREF bkcolor = RGB(40, 40, 40);
// background color 2
COLORREF bkcolor2 = RGB(25, 25, 25);
// text color
COLORREF textcolor = RGB(220, 220, 220);
// down arrow color in dropdown
COLORREF arrowcolor = RGB(200, 200, 200);

// prototypes of static functions
static string get_window_text(HWND hwnd);
static void draw_combo_text(HWND hwnd, HDC hdc, RECT rc);
static LRESULT CALLBACK version_combo_subproc(HWND hwnd, UINT msg, WPARAM wParam,
    LPARAM lParam, UINT_PTR uIdSubClass, DWORD_PTR);
static void do_create(HWND hwnd);
static void do_destroy(HWND hwnd);
static void do_paint(HWND hwnd);
static LRESULT do_button_paint(WPARAM wParam, LPARAM lParam);
static void do_save_config();
static void do_inject();
static void handle_click(HWND hwnd, WPARAM wParam, LPARAM lParam);
static void handle_selection_change(HWND hwnd, WPARAM wParam, LPARAM lParam);
static void handle_edit_change(HWND hwnd, WPARAM wParam, LPARAM lParam);
static void do_command(HWND hwnd, WPARAM wParam, LPARAM lParam);
static LRESULT CALLBACK window_proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

// program entry
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR pCmdLine, int nCmdShow)
{
    RECT desktop;
    WNDCLASS wc;
    HWND hwnd;
    MSG msg;

    // class registration
    memset(&msg, 0, sizeof(msg));
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = window_proc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "ExportTracer";
    RegisterClass(&wc);

    // get desktop rect so we can center the window
    GetClientRect(GetDesktopWindow(), &desktop);

    // create the window
    hwnd = CreateWindow(wc.lpszClassName, "Export Tracer",
        WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, 
        (desktop.right/2) - 600, (desktop.bottom/2) - 240, 
        420, 210, NULL, NULL, hInstance, NULL);
    if (!hwnd) {
        MessageBox(NULL, "Failed to open window", "Error", 0);
        return 0;
    }

    // main message loop
    ShowWindow(hwnd, nCmdShow);
    while (GetMessage(&msg, NULL, 0, 0)) {
        if (!IsDialogMessage(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    return 0;
}

// helper to read text from a textbox
static string get_window_text(HWND hwnd)
{
    string result;
    char *buf = NULL;
    if (!hwnd) {
        return result;
    }
    int len = GetWindowTextLength(hwnd) + 1;
    buf = new char[len];
    if (!buf) {
        return result;
    }
    GetWindowText(hwnd, buf, len);
    result = buf;
    delete[] buf;
    return result;
}

// draw handler for the version combo box
static void draw_combo_text(HWND hwnd, HDC hdc, RECT rc)
{
    // select font and text color
    SelectObject(hdc, (HFONT)SendMessage(hwnd, WM_GETFONT, 0, 0));
    SetTextColor(hdc, textcolor);
    // need to redraw the text as long as the dropdown is open, win32 sucks
    int index = ComboBox_GetCurSel(hwnd);
    if (index >= 0) {
        size_t buflen = ComboBox_GetLBTextLen(hwnd, index);
        char *buf = new char[(buflen + 1)];
        ComboBox_GetLBText(hwnd, index, buf);
        rc.left += 4;
        DrawText(hdc, buf, -1, &rc, DT_EDITCONTROL | DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        delete[] buf;
    }
}

// subprocess for version combobox
static LRESULT CALLBACK version_combo_subproc(HWND hwnd, UINT msg, WPARAM wParam,
    LPARAM lParam, UINT_PTR uIdSubClass, DWORD_PTR)
{
    if (msg != WM_PAINT) {
        return DefSubclassProc(hwnd, msg, wParam, lParam);
    }
    // the vertices for the dropdown triangle
    static const POINT vertices[] = { 
        {122, 10}, {132, 10}, {127, 15} 
    };
    HGDIOBJ oldbrush;
    HGDIOBJ oldpen;
    PAINTSTRUCT ps;
    HDC hdc;
    RECT rc;

    hdc = BeginPaint(hwnd, &ps);
    GetClientRect(hwnd, &rc);

    // set background brush and border pen
    oldbrush = SelectObject(hdc, bkbrush2);
    oldpen = SelectObject(hdc, borderpen);
    // set background color
    SetBkColor(hdc, bkcolor2);

    // draw the two rectangles
    Rectangle(hdc, 0, 0, rc.right, rc.bottom);
    // redraw the text 
    draw_combo_text(hwnd, hdc, rc);
    // draw the box around the dropdown button part
    Rectangle(hdc, rc.right - 25, rc.top + 2, rc.right - 24, rc.bottom - 2);

    // select pen and brush for drawing down arrow
    SelectObject(hdc, arrowbrush);
    SelectObject(hdc, arrowpen);
    // draw the down arrow
    SetPolyFillMode(hdc, ALTERNATE);
    Polygon(hdc, vertices, sizeof(vertices) / sizeof(vertices[0]));

    // restore old brush and pen
    SelectObject(hdc, oldbrush);
    SelectObject(hdc, oldpen);

    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

// launch button
HWND hwndLaunchButton;
#define LAUNCH_BUTTON_ID            1001

// path textbox
HWND hwndPathEdit;
#define PATH_EDIT_ID                1002


static void do_create(HWND hwnd)
{
    HANDLE hFont = CreateFont(14, 0, 0, 0, 400, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, FIXED_PITCH | FF_DONTCARE, "System");

    // create the install path entry text box
    hwndPathEdit = CreateWindow(WC_EDIT, "C:\\windows\\system32\\mspaint.exe", 
        WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, 
        12, 42, 378, 21, hwnd, (HMENU)PATH_EDIT_ID, NULL, NULL);

    // create the launch button
    hwndLaunchButton = CreateWindow(WC_BUTTON, "Launch",
        WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON | WS_TABSTOP,
        150, 80, 120, 40, hwnd, (HMENU)LAUNCH_BUTTON_ID, NULL, NULL);
    // Set font
    SendMessage(hwndLaunchButton, WM_SETFONT, (WPARAM)hFont, TRUE);

    bkbrush = CreateSolidBrush(bkcolor);
    bkbrush2 = CreateSolidBrush(bkcolor2);
    arrowbrush = CreateSolidBrush(arrowcolor);
    borderpen = CreatePen(PS_SOLID, 1, RGB(0, 0, 0));
    arrowpen = CreatePen(PS_SOLID, 1, arrowcolor);
}

static void do_destroy(HWND hwnd)
{
    DeleteObject(bkbrush);
    DeleteObject(bkbrush2);
    DeleteObject(arrowbrush);
    DeleteObject(borderpen);
    DeleteObject(arrowpen);
}

static void do_paint(HWND hwnd)
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    FillRect(hdc, &ps.rcPaint, (HBRUSH)bkbrush);
    EndPaint(hwnd, &ps);
}

static LRESULT do_button_paint(WPARAM wParam, LPARAM lParam)
{
    HDC hdc = (HDC)wParam;
    SetBkColor(hdc, bkcolor2);
    SetTextColor(hdc, textcolor);
    // labels, radio buttons, and checkboxes need transparency set
    // otherwise they have an ugly box around the text
    return (LRESULT)bkbrush2;
}

static string get_launcher_folder()
{
    char buffer[MAX_PATH] = { 0 };
    // grab path of current executable
    DWORD len = GetModuleFileName(NULL, buffer, MAX_PATH);
    // strip off the filename
    if (!len || !PathRemoveFileSpec(buffer)) {
        MessageBoxA(NULL, "Failed to resolve module path", "Error", 0);
        return string();
    }
    return string(buffer) + "\\";
}

static void handle_click(HWND hwnd, WPARAM wParam, LPARAM lParam)
{
    Injector injector;
    // most of the clicks utilize the current selection
    //int sel = ListBox_GetCurSel(hwndOutfilesList);
    switch (LOWORD(wParam)) {
    case LAUNCH_BUTTON_ID:
        injector.init(get_window_text(hwndPathEdit),
                      get_launcher_folder() + "ExportTracer.dll");
        injector.inject();
        break;
    default:
        break;
    }
}

static void handle_selection_change(HWND hwnd, WPARAM wParam, LPARAM lParam)
{
}

static void handle_edit_change(HWND hwnd, WPARAM wParam, LPARAM lParam)
{
}

static void do_command(HWND hwnd, WPARAM wParam, LPARAM lParam)
{
    switch (HIWORD(wParam)) {
    case BN_CLICKED:
        handle_click(hwnd, wParam, lParam);
        break;
    case LBN_SELCHANGE: 
    //case CBN_SELCHANGE:
        // supposed to catch CBN_SELCHANGE for combobox and LBN_SELCHANGE 
        // for listbox but they literally both expand to 1 so we can't
        // even have both in the same switch statement
        handle_selection_change(hwnd, wParam, lParam);
        break;
    case EN_CHANGE:
        handle_edit_change(hwnd, wParam, lParam);
        break;
    }
}

static LRESULT CALLBACK window_proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg) {
    case WM_COMMAND:
        do_command(hwnd, wParam, lParam);
        break;
    case WM_INITDIALOG:
        break;
    case WM_CREATE:
        do_create(hwnd);
        break;
    case WM_DESTROY:
        do_destroy(hwnd);
        PostQuitMessage(0);
        break;
    case WM_PAINT:
        do_paint(hwnd);
        return 0;
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
        return do_button_paint(wParam, lParam);
    default:
        break;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}
