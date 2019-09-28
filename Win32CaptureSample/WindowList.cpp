#include "pch.h"
#include "WindowList.h"
#include <dwmapi.h>

bool IsCapturableWindow(const WindowInfo const& window)
{
    auto hwnd = window.WindowHandle;
    auto shellWindow = GetShellWindow();

    auto title = window.Title;

    if (hwnd == shellWindow)
    {
        return false;
    }

    if (title.length() == 0)
    {
        return false;
    }

    if (!IsWindowVisible(hwnd))
    {
        return false;
    }

    if (GetAncestor(hwnd, GA_ROOT) != hwnd)
    {
        return false;
    }

    LONG style = GetWindowLongW(hwnd, GWL_STYLE);
    if (!((style & WS_DISABLED) != WS_DISABLED))
    {
        return false;
    }

    DWORD cloaked = FALSE;
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && (cloaked == DWM_CLOAKED_SHELL))
    {
        return false;
    }

    // Unfortunate work-around. Not sure how to avoid this.
    if (wcscmp(title.c_str(), L"Task View") == 0)
    {
        return false;
    }

    return true;
}

static thread_local WindowList* WindowListForThread;

WindowList::WindowList()
{
    if (WindowListForThread)
    {
        throw std::exception("WindowList already exists for this thread!");
    }
    WindowListForThread = this;

    EnumWindows([](HWND hwnd, LPARAM lParam)
    {
        if (GetWindowTextLengthW(hwnd) > 0)
        {
            auto window = WindowInfo(hwnd);

            if (!IsCapturableWindow(window))
            {
                return TRUE;
            }

            auto windowList = reinterpret_cast<WindowList*>(lParam);
            windowList->AddWindow(window);
        }
        
        return TRUE;
    }, reinterpret_cast<LPARAM>(this));
    
    // TODO: Handle desktop switching (new windows)
    m_eventHook.reset(SetWinEventHook(EVENT_OBJECT_DESTROY, /*EVENT_OBJECT_SHOW*/EVENT_OBJECT_UNCLOAKED, nullptr,
        [](HWINEVENTHOOK eventHook, DWORD event, HWND hwnd, LONG objectId, LONG childId, DWORD eventThreadId, DWORD eventTimeInMilliseconds)
        {
            if (event == EVENT_OBJECT_DESTROY && childId == CHILDID_SELF)
            {
                WindowListForThread->RemoveWindow(WindowInfo(hwnd));
                return;
            }

            if (objectId == OBJID_WINDOW && childId == CHILDID_SELF && hwnd != nullptr && GetAncestor(hwnd, GA_ROOT) == hwnd &&
                GetWindowTextLengthW(hwnd) > 0 && (event == EVENT_OBJECT_SHOW || event == EVENT_OBJECT_UNCLOAKED))
            {
                auto window = WindowInfo(hwnd);

                if (IsCapturableWindow(window))
                {
                    WindowListForThread->AddWindow(window);
                }
            }
        }, 0, 0, WINEVENT_OUTOFCONTEXT));
}

WindowList::~WindowList()
{
    m_eventHook.reset();
    WindowListForThread = nullptr;
}

void WindowList::AddWindow(WindowInfo const& info)
{
    auto search = m_seenWindows.find(info.WindowHandle);
    if (search == m_seenWindows.end())
    {
        m_windows.push_back(info);
        m_seenWindows.insert(info.WindowHandle);
        for (auto& comboBox : m_comboBoxes)
        {
            winrt::check_hresult(SendMessageW(comboBox, CB_ADDSTRING, 0, (LPARAM)info.Title.c_str()));
        }
    }
}

bool WindowList::RemoveWindow(WindowInfo const& info)
{
    auto search = m_seenWindows.find(info.WindowHandle);
    if (search != m_seenWindows.end())
    {
        m_seenWindows.erase(search);
        auto index = 0;
        for (auto& window : m_windows)
        {
            if (window.WindowHandle == info.WindowHandle)
            {
                break;
            }
            index++;
        }
        m_windows.erase(m_windows.begin() + index);
        for (auto& comboBox : m_comboBoxes)
        {
            winrt::check_hresult(SendMessageW(comboBox, CB_DELETESTRING, index, 0));
        }
        return index >= 0;
    }
    return false;
}

void WindowList::ForceUpdateComboBox(HWND comboBoxHandle)
{
    winrt::check_hresult(SendMessageW(comboBoxHandle, CB_RESETCONTENT, 0, 0));
    for (auto& window : m_windows)
    {
        winrt::check_hresult(SendMessageW(comboBoxHandle, CB_ADDSTRING, 0, (LPARAM)window.Title.c_str()));
    }
}