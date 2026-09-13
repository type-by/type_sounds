#define UNICODE
#define _UNICODE
#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <psapi.h>

#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "msimg32.lib")
#pragma comment(lib, "psapi.lib")

#define MAX_LINES 600
#define MAX_PLAYLIST 64
#define TIMER_ID 1
#define WATCHER_TIMER_ID 2
#define WM_PYTHON_DONE (WM_USER + 101)

typedef struct {
    long timestamp_ms;
    wchar_t original[256];
    wchar_t translation[256];
} LyricLine;

LyricLine g_lyrics[MAX_LINES];
int g_lyric_count = 0;
int g_current_index = 0;
BOOL g_is_playing = FALSE;
long g_total_duration_ms = 0;
long g_current_pos_ms = 0;
BOOL g_is_dragging_progress = FALSE;
BOOL g_is_dragging_volume = FALSE;

// Spotify Tarzı Akıcı Süzülme (Lerp)
double g_current_scroll_y = 0.0;
double g_target_scroll_y = 0.0;

wchar_t g_song_title[128] = L"Parça Seçin...";
wchar_t g_artist_name[128] = L"Sağdaki Listeden Tıklayın";
wchar_t g_active_song_path[MAX_PATH] = L"";
BOOL g_is_fetching = FALSE;

wchar_t g_playlist[MAX_PLAYLIST][MAX_PATH];
int g_playlist_count = 0;
int g_selected_track_idx = -1;

BOOL g_is_fullscreen = FALSE;
RECT g_prev_window_rect = {0};
BOOL g_eco_mode = FALSE;
int g_volume_level = 850;
BOOL g_show_settings_modal = FALSE;

// 1. ARAYÜZ DİLLERİ TANIMI
typedef struct {
    const wchar_t *name;
    const char *code;
    const wchar_t *title_settings;
    const wchar_t *btn_settings;
    const wchar_t *fullscreen_txt;
    const wchar_t *eco_on_txt;
    const wchar_t *eco_off_txt;
    const wchar_t *active_trans_txt;
    const wchar_t *sec_ui_lang;
    const wchar_t *sec_trans_lang;
    const wchar_t *queue_title;
    const wchar_t *queue_sub;
    const wchar_t *badge_txt;
    const wchar_t *close_btn;
} UILang;

UILang g_ui_langs[5] = {
    { L"Türkçe", "tr", L"Uygulama Ayarları", L"⚙ Ayarlar & Dil Seç", L"Tam Ekran", L"Eco: Açık", L"Eco [F2]", L"★ Dil Çevirisi Aktif", L"Ana Dilin (Arayüz):", L"Öğrenilen Dil (Çeviri Hedefi):", L"Çalma Sırası", L"music/ klasöründeki parçalar", L"CANLI LİRİK & TELAFFUZ", L"Kaydet ve Kapat" },
    { L"English", "en", L"Settings", L"⚙ Settings & Language", L"Fullscreen", L"Eco: ON", L"Eco [F2]", L"★ Translation Active", L"My Native Language (UI):", L"Learning Language (Translate To):", L"Play Queue", L"tracks in music/ folder", L"LIVE LYRICS & PRONUNCIATION", L"Save & Close" },
    { L"Русский", "ru", L"Настройки", L"⚙ Настройки и Язык", L"Полный экран", L"Эко: Вкл", L"Эко [F2]", L"★ Перевод активен", L"Родной язык (Интерфейс):", L"Изучаемый язык (Перевод):", L"Очередь", L"треки из папки music/", L"ЖИВЫЕ ТЕКСТЫ И ПЕРЕВОД", L"Сохранить и закрыть" },
    { L"Deutsch", "de", L"Einstellungen", L"⚙ Sprache & Optionen", L"Vollbild", L"Öko: An", L"Öko [F2]", L"★ Übersetzung Aktiv", L"Muttersprache (UI):", L"Lernsprache (Übersetzung):", L"Wiedergabeliste", L"Titel im music/ Ordner", L"LIVE-LYRICS & AUSSPRACHE", L"Speichern & Schließen" },
    { L"עברית", "he", L"הגדרות", L"⚙ הגדרות ושפה", L"מסך מלא", L"אקו: פעיל", L"אקו [F2]", L"★ תרגום פעיל", L"שפת ממשק:", L"שפת תרגום:", L"רשימת השמעה", L"שירים בתיקיית music/", L"מילים חיות ותרגום", L"שמור וסגור" }
};

const wchar_t *g_trans_names[5] = { L"Türkçe", L"English", L"Русский", L"Deutsch", L"עברית" };
const char *g_trans_codes[5] = { "tr", "en", "ru", "de", "he" };

int g_selected_ui_lang = 0;    
int g_selected_trans_lang = 0; 

long ParseTimestamp(const char *str) {
    int min = 0, sec = 0, hun = 0;
    if (sscanf(str, "[%d:%d.%d]", &min, &sec, &hun) >= 2) {
        return (min * 60 * 1000) + (sec * 1000) + (hun * 10);
    }
    return -1;
}

void LoadLanguageConfig() {
    FILE *fp = fopen("lang.cfg", "r");
    if (fp) {
        char uiCode[16] = {0}, trCode[16] = {0};
        if (fscanf(fp, "%15s %15s", uiCode, trCode) >= 1) {
            for (int i = 0; i < 5; i++) {
                if (strcmp(uiCode, g_ui_langs[i].code) == 0) g_selected_ui_lang = i;
                if (strcmp(trCode, g_trans_codes[i]) == 0) g_selected_trans_lang = i;
            }
        }
        fclose(fp);
    }
}

void SaveLanguageConfig() {
    FILE *fp = fopen("lang.cfg", "w");
    if (fp) {
        fprintf(fp, "%s %s", g_ui_langs[g_selected_ui_lang].code, g_trans_codes[g_selected_trans_lang]);
        fclose(fp);
    }
}

void ApplyWorkingSetReduction() {
    HANDLE hProcess = GetCurrentProcess();
    SetProcessWorkingSetSize(hProcess, (SIZE_T)-1, (SIZE_T)-1);
    EmptyWorkingSet(hProcess);
}

void SetVolume(int vol) {
    if (vol < 0) vol = 0;
    if (vol > 1000) vol = 1000;
    g_volume_level = vol;

    wchar_t cmd[64];
    wsprintfW(cmd, L"setaudio song volume to %d", g_volume_level);
    mciSendStringW(cmd, NULL, 0, NULL);
}

void LoadLRC() {
    FILE *fp = fopen("lyrics.lrc", "r");
    if (!fp) return;

    char line[512];
    g_lyric_count = 0;

    while (fgets(line, sizeof(line), fp) && g_lyric_count < MAX_LINES) {
        line[strcspn(line, "\r\n")] = 0;

        if (strncmp(line, "[ti:", 4) == 0) {
            char *end = strchr(line + 4, ']');
            if (end) *end = '\0';
            MultiByteToWideChar(CP_UTF8, 0, line + 4, -1, g_song_title, 128);
            continue;
        }
        if (strncmp(line, "[ar:", 4) == 0) {
            char *end = strchr(line + 4, ']');
            if (end) *end = '\0';
            MultiByteToWideChar(CP_UTF8, 0, line + 4, -1, g_artist_name, 128);
            continue;
        }

        if (line[0] == '[') {
            long ms = ParseTimestamp(line);
            if (ms >= 0) {
                char *text_start = strchr(line, ']');
                if (text_start) {
                    text_start++;
                    while (*text_start == ' ') text_start++;

                    g_lyrics[g_lyric_count].timestamp_ms = ms;

                    char *paren = strchr(text_start, '(');
                    if (paren) {
                        char orig[256] = {0};
                        char trans[256] = {0};
                        int len = (int)(paren - text_start);
                        if (len > 255) len = 255;
                        strncpy(orig, text_start, len);
                        orig[len] = '\0';

                        while (len > 0 && orig[len - 1] == ' ') { orig[--len] = '\0'; }

                        char *paren_end = strchr(paren, ')');
                        if (paren_end) {
                            int tlen = (int)(paren_end - (paren + 1));
                            if (tlen > 255) tlen = 255;
                            strncpy(trans, paren + 1, tlen);
                            trans[tlen] = '\0';
                        }

                        MultiByteToWideChar(CP_UTF8, 0, orig, -1, g_lyrics[g_lyric_count].original, 256);
                        MultiByteToWideChar(CP_UTF8, 0, trans, -1, g_lyrics[g_lyric_count].translation, 256);
                    } else {
                        MultiByteToWideChar(CP_UTF8, 0, text_start, -1, g_lyrics[g_lyric_count].original, 256);
                        g_lyrics[g_lyric_count].translation[0] = L'\0';
                    }
                    g_lyric_count++;
                }
            }
        }
    }
    fclose(fp);
}

long QueryMCI(const wchar_t *cmd) {
    wchar_t response[128] = {0};
    mciSendStringW(cmd, response, sizeof(response)/sizeof(wchar_t), NULL);
    return _wtol(response);
}

void SeekToMs(long ms) {
    if (ms < 0) ms = 0;
    if (ms > g_total_duration_ms && g_total_duration_ms > 0) ms = g_total_duration_ms;
    g_current_pos_ms = ms;

    wchar_t cmd[64];
    wsprintfW(cmd, L"seek song to %ld", ms);
    mciSendStringW(cmd, NULL, 0, NULL);
    if (g_is_playing) {
        mciSendStringW(L"play song", NULL, 0, NULL);
    }
}

void FormatTime(long ms, wchar_t *buf) {
    long sec = ms / 1000;
    long m = sec / 60;
    long s = sec % 60;
    wsprintfW(buf, L"%02ld:%02ld", m, s);
}

void GetProgressBarRect(RECT *rcClient, RECT *rcBar) {
    int barW = 440;
    int midX = rcClient->right / 2;
    int barY = rcClient->bottom - 30;
    rcBar->left = midX - (barW / 2);
    rcBar->right = midX + (barW / 2);
    rcBar->top = barY;
    rcBar->bottom = barY + 5;
}

void GetVolumeBarRect(RECT *rcClient, RECT *rcVol) {
    rcVol->right = rcClient->right - 40;
    rcVol->left = rcVol->right - 100;
    rcVol->top = rcClient->bottom - 30;
    rcVol->bottom = rcClient->bottom - 25;
}

void ToggleFullscreen(HWND hwnd) {
    DWORD style = GetWindowLong(hwnd, GWL_STYLE);
    if (!g_is_fullscreen) {
        GetWindowRect(hwnd, &g_prev_window_rect);
        MONITORINFO mi = { sizeof(mi) };
        if (GetMonitorInfo(MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY), &mi)) {
            SetWindowLong(hwnd, GWL_STYLE, (style & ~WS_OVERLAPPEDWINDOW) | WS_POPUP);
            SetWindowPos(hwnd, HWND_TOP,
                         mi.rcMonitor.left, mi.rcMonitor.top,
                         mi.rcMonitor.right - mi.rcMonitor.left,
                         mi.rcMonitor.bottom - mi.rcMonitor.top,
                         SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
            g_is_fullscreen = TRUE;
        }
    } else {
        SetWindowLong(hwnd, GWL_STYLE, (style & ~WS_POPUP) | WS_OVERLAPPEDWINDOW);
        SetWindowPos(hwnd, NULL,
                     g_prev_window_rect.left, g_prev_window_rect.top,
                     g_prev_window_rect.right - g_prev_window_rect.left,
                     g_prev_window_rect.bottom - g_prev_window_rect.top,
                     SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        g_is_fullscreen = FALSE;
    }
    InvalidateRect(hwnd, NULL, TRUE);
}

DWORD WINAPI FetchLyricsThread(LPVOID param) {
    HWND hwnd = (HWND)param;
    STARTUPINFOA si = { sizeof(STARTUPINFOA) };
    PROCESS_INFORMATION pi;
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    char cmd[512];
    char trackA[256] = "";
    if (g_selected_track_idx >= 0 && g_selected_track_idx < g_playlist_count) {
        WideCharToMultiByte(CP_UTF8, 0, g_playlist[g_selected_track_idx], -1, trackA, 256, NULL, NULL);
        char *dot = strrchr(trackA, '.');
        if (dot) *dot = '\0';
    }

    sprintf(cmd, "python fetch_lyrics.py %s \"%s\"", g_trans_codes[g_selected_trans_lang], trackA);

    if (CreateProcessA(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 10000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    PostMessage(hwnd, WM_PYTHON_DONE, 0, 0);
    return 0;
}

void StartSongByIndex(HWND hwnd, int idx) {
    if (idx < 0 || idx >= g_playlist_count || g_is_fetching) return;
    g_selected_track_idx = idx;

    wsprintfW(g_active_song_path, L"music\\%s", g_playlist[idx]);
    g_is_fetching = TRUE;

    wcsncpy(g_song_title, g_playlist[idx], 120);
    wchar_t *dot = wcsrchr(g_song_title, L'.');
    if (dot) *dot = L'\0';
    wcscpy(g_artist_name, L"Translating & Syncing...");

    InvalidateRect(hwnd, NULL, TRUE);
    CreateThread(NULL, 0, FetchLyricsThread, (LPVOID)hwnd, 0, NULL);
}

void ScanPlaylist(HWND hwnd, BOOL autoStart) {
    WIN32_FIND_DATAW findData;
    HANDLE hFind = FindFirstFileW(L"music\\*.mp3", &findData);

    wchar_t temp_list[MAX_PLAYLIST][MAX_PATH];
    int count = 0;

    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (count < MAX_PLAYLIST) {
                wcscpy(temp_list[count], findData.cFileName);
                count++;
            }
        } while (FindNextFileW(hFind, &findData));
        FindClose(hFind);
    }

    BOOL changed = (count != g_playlist_count);
    if (!changed) {
        for (int i = 0; i < count; i++) {
            if (wcscmp(temp_list[i], g_playlist[i]) != 0) {
                changed = TRUE;
                break;
            }
        }
    }

    if (changed) {
        g_playlist_count = count;
        for (int i = 0; i < count; i++) {
            wcscpy(g_playlist[i], temp_list[i]);
        }
        if (autoStart && g_selected_track_idx == -1 && g_playlist_count > 0) {
            StartSongByIndex(hwnd, 0);
        }
        InvalidateRect(hwnd, NULL, TRUE);
    }
}

HICON CreateHighDefAppIcon() {
    int sz = 32;
    BITMAPINFO bi = {0};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = sz;
    bi.bmiHeader.biHeight = -sz;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void *pBits = NULL;
    HDC hdc = GetDC(NULL);
    HBITMAP hColorBmp = CreateDIBSection(hdc, &bi, DIB_RGB_COLORS, &pBits, NULL, 0);
    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP hOld = (HBITMAP)SelectObject(memDC, hColorBmp);

    HBRUSH bgBrush = CreateSolidBrush(RGB(15, 15, 17));
    RECT r = {0, 0, sz, sz};
    FillRect(memDC, &r, bgBrush);
    DeleteObject(bgBrush);

    HBRUSH greenBrush = CreateSolidBrush(RGB(29, 185, 84));
    HPEN nullPen = CreatePen(PS_NULL, 0, RGB(0,0,0));
    SelectObject(memDC, greenBrush);
    SelectObject(memDC, nullPen);
    RoundRect(memDC, 2, 2, sz - 2, sz - 2, 10, 10);

    HPEN whitePen = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
    SelectObject(memDC, whitePen);

    MoveToEx(memDC, 8, 20, NULL); LineTo(memDC, 8, 12);
    MoveToEx(memDC, 13, 23, NULL); LineTo(memDC, 13, 9);
    MoveToEx(memDC, 18, 21, NULL); LineTo(memDC, 18, 11);
    MoveToEx(memDC, 23, 19, NULL); LineTo(memDC, 23, 13);

    SelectObject(memDC, hOld);
    DeleteDC(memDC);
    ReleaseDC(NULL, hdc);
    DeleteObject(whitePen);
    DeleteObject(greenBrush);
    DeleteObject(nullPen);

    HBITMAP hMask = CreateBitmap(sz, sz, 1, 1, NULL);
    ICONINFO ii = {0};
    ii.fIcon = TRUE;
    ii.hbmColor = hColorBmp;
    ii.hbmMask = hMask;

    HICON hIcon = CreateIconIndirect(&ii);
    DeleteObject(hColorBmp);
    DeleteObject(hMask);
    return hIcon;
}

void RenderSpotifyDesktop(HWND hwnd, HDC hdc) {
    RECT rc;
    GetClientRect(hwnd, &rc);

    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBitmap = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
    HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);

    HBRUSH bgBlack = CreateSolidBrush(RGB(13, 14, 18));
    FillRect(memDC, &rc, bgBlack);
    DeleteObject(bgBlack);

    SetBkMode(memDC, TRANSPARENT);

    HFONT fontSideBig = CreateFontW(22, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                   DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                   CLEARTYPE_NATURAL_QUALITY, DEFAULT_PITCH, L"Segoe UI");

    HFONT fontSideBtn = CreateFontW(17, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                   DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                   CLEARTYPE_NATURAL_QUALITY, DEFAULT_PITCH, L"Segoe UI");

    HFONT fontSideText = CreateFontW(16, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                    CLEARTYPE_NATURAL_QUALITY, DEFAULT_PITCH, L"Segoe UI");

    HFONT fontTitle = CreateFontW(24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  CLEARTYPE_NATURAL_QUALITY, DEFAULT_PITCH, L"Segoe UI");

    HFONT fontActive = CreateFontW(28, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                   DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                   CLEARTYPE_NATURAL_QUALITY, DEFAULT_PITCH, L"Segoe UI");

    HFONT fontActiveSub = CreateFontW(20, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                      CLEARTYPE_NATURAL_QUALITY, DEFAULT_PITCH, L"Segoe UI");

    HFONT fontDim = CreateFontW(20, 0, 0, 0, FW_MEDIUM, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                CLEARTYPE_NATURAL_QUALITY, DEFAULT_PITCH, L"Segoe UI");

    HFONT fontSmall = CreateFontW(13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  CLEARTYPE_NATURAL_QUALITY, DEFAULT_PITCH, L"Segoe UI");

    HPEN nullPen = CreatePen(PS_NULL, 0, RGB(0,0,0));
    HPEN oldPen = (HPEN)SelectObject(memDC, nullPen);

    // ==========================================
    // 1. SOL PANEL
    // ==========================================
    int sideWidth = 240;
    HBRUSH sideBrush = CreateSolidBrush(RGB(19, 21, 26));
    SelectObject(memDC, sideBrush);
    RoundRect(memDC, 8, 8, sideWidth, rc.bottom - 95, 14, 14);
    DeleteObject(sideBrush);

    SelectObject(memDC, fontSideBig);
    SetTextColor(memDC, RGB(255, 255, 255));
    RECT sideTitle = { 20, 24, sideWidth - 15, 52 };
    DrawTextW(memDC, L"Aura", -1, &sideTitle, DT_LEFT | DT_SINGLELINE);

    HBRUSH setBtnBrush = CreateSolidBrush(g_show_settings_modal ? RGB(29, 185, 84) : RGB(34, 38, 48));
    SelectObject(memDC, setBtnBrush);
    RoundRect(memDC, 18, 62, sideWidth - 18, 108, 16, 16);
    DeleteObject(setBtnBrush);

    SelectObject(memDC, fontSideBtn);
    SetTextColor(memDC, g_show_settings_modal ? RGB(0, 0, 0) : RGB(255, 255, 255));
    RECT setBtnText = { 18, 62, sideWidth - 18, 108 };
    DrawTextW(memDC, g_ui_langs[g_selected_ui_lang].btn_settings, -1, &setBtnText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    HBRUSH pillBrush = CreateSolidBrush(RGB(30, 33, 42));
    SelectObject(memDC, pillBrush);
    RoundRect(memDC, 18, 118, 115, 158, 14, 14);
    RoundRect(memDC, 122, 118, sideWidth - 18, 158, 14, 14);
    DeleteObject(pillBrush);

    SelectObject(memDC, fontSideText);
    SetTextColor(memDC, g_is_fullscreen ? RGB(29, 185, 84) : RGB(200, 205, 215));
    RECT pill1Text = { 18, 118, 115, 158 };
    DrawTextW(memDC, g_ui_langs[g_selected_ui_lang].fullscreen_txt, -1, &pill1Text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    SetTextColor(memDC, g_eco_mode ? RGB(29, 185, 84) : RGB(200, 205, 215));
    RECT pill2Text = { 122, 118, sideWidth - 18, 158 };
    DrawTextW(memDC, g_eco_mode ? g_ui_langs[g_selected_ui_lang].eco_on_txt : g_ui_langs[g_selected_ui_lang].eco_off_txt, -1, &pill2Text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    SelectObject(memDC, fontSideText);
    SetTextColor(memDC, RGB(29, 185, 84));
    RECT item1 = { 20, 180, sideWidth - 15, 205 };
    DrawTextW(memDC, g_ui_langs[g_selected_ui_lang].active_trans_txt, -1, &item1, DT_LEFT | DT_SINGLELINE);

    SetTextColor(memDC, RGB(185, 190, 205));
    RECT item2 = { 20, 210, sideWidth - 15, 280 };
    wchar_t infoBuf[160];
    wsprintfW(infoBuf, L"UI: %s\nTarget: %s\nTracks: %d", 
              g_ui_langs[g_selected_ui_lang].name, 
              g_trans_names[g_selected_trans_lang], 
              g_playlist_count);
    DrawTextW(memDC, infoBuf, -1, &item2, DT_LEFT);

    // ==========================================
    // 2. SAĞ PANEL (ÇALMA SIRASI)
    // ==========================================
    int rightPanelWidth = 260;
    int rightPanelLeft = rc.right - rightPanelWidth - 8;

    HBRUSH rightBrush = CreateSolidBrush(RGB(19, 21, 26));
    SelectObject(memDC, rightBrush);
    RoundRect(memDC, rightPanelLeft, 8, rc.right - 8, rc.bottom - 95, 14, 14);
    DeleteObject(rightBrush);

    SelectObject(memDC, fontSideBig);
    SetTextColor(memDC, RGB(255, 255, 255));
    RECT rightTitle = { rightPanelLeft + 18, 22, rc.right - 18, 48 };
    DrawTextW(memDC, g_ui_langs[g_selected_ui_lang].queue_title, -1, &rightTitle, DT_LEFT | DT_SINGLELINE);

    SelectObject(memDC, fontSmall);
    SetTextColor(memDC, RGB(29, 185, 84));
    RECT rightSub = { rightPanelLeft + 18, 48, rc.right - 18, 66 };
    DrawTextW(memDC, g_ui_langs[g_selected_ui_lang].queue_sub, -1, &rightSub, DT_LEFT | DT_SINGLELINE);

    int listTop = 75;
    int trackItemHeight = 44;
    for (int t = 0; t < g_playlist_count; t++) {
        int itemY = listTop + t * trackItemHeight;
        if (itemY + trackItemHeight > rc.bottom - 105) break;

        RECT itemRect = { rightPanelLeft + 12, itemY, rc.right - 16, itemY + trackItemHeight - 4 };

        if (t == g_selected_track_idx) {
            HBRUSH activeTrackBrush = CreateSolidBrush(RGB(24, 48, 36));
            SelectObject(memDC, activeTrackBrush);
            RoundRect(memDC, itemRect.left, itemRect.top, itemRect.right, itemRect.bottom, 10, 10);
            DeleteObject(activeTrackBrush);

            SelectObject(memDC, fontSideText);
            SetTextColor(memDC, RGB(29, 185, 84));
        } else {
            HBRUSH idleTrackBrush = CreateSolidBrush(RGB(26, 28, 36));
            SelectObject(memDC, idleTrackBrush);
            RoundRect(memDC, itemRect.left, itemRect.top, itemRect.right, itemRect.bottom, 10, 10);
            DeleteObject(idleTrackBrush);

            SelectObject(memDC, fontSideText);
            SetTextColor(memDC, RGB(210, 215, 225));
        }

        RECT textR = { itemRect.left + 12, itemRect.top + 10, itemRect.right - 10, itemRect.bottom - 5 };
        DrawTextW(memDC, g_playlist[t], -1, &textR, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    // ==========================================
    // 3. ORTA PANEL (LİRİK SAHNESİ)
    // ==========================================
    int mainLeft = sideWidth + 8;
    int mainRight = rightPanelLeft - 8;

    HBRUSH mainBrush = CreateSolidBrush(RGB(19, 21, 26));
    SelectObject(memDC, mainBrush);
    RoundRect(memDC, mainLeft, 8, mainRight, rc.bottom - 95, 14, 14);
    DeleteObject(mainBrush);

    SelectObject(memDC, fontSideText);
    SetTextColor(memDC, RGB(29, 185, 84));
    RECT badgeRc = { mainLeft + 35, 22, mainRight - 35, 45 };
    wchar_t badgeBuf[128];
    wsprintfW(badgeBuf, L"%s • %s", g_ui_langs[g_selected_ui_lang].badge_txt, g_trans_names[g_selected_trans_lang]);
    DrawTextW(memDC, badgeBuf, -1, &badgeRc, DT_LEFT | DT_SINGLELINE);

    SelectObject(memDC, fontTitle);
    SetTextColor(memDC, RGB(255, 255, 255));
    RECT trackRc = { mainLeft + 35, 46, mainRight - 35, 78 };
    wchar_t fullTitle[256];
    wsprintfW(fullTitle, L"%s — %s", g_song_title, g_artist_name);
    DrawTextW(memDC, fullTitle, -1, &trackRc, DT_LEFT | DT_SINGLELINE);

    int centerY = (rc.bottom - 95) / 2 + 10;
    int lineHeight = 66;

    if (g_lyric_count > 0) {
        for (int i = 0; i < g_lyric_count; i++) {
            int offsetY = (int)(centerY + (i * lineHeight) - g_current_scroll_y);
            if (offsetY < 95 || offsetY > rc.bottom - 140) continue;

            RECT textRect = { mainLeft + 35, offsetY, mainRight - 35, offsetY + 34 };
            RECT transRect = { mainLeft + 35, offsetY + 34, mainRight - 35, offsetY + 64 };

            if (i == g_current_index) {
                SelectObject(memDC, fontActive);
                SetTextColor(memDC, RGB(255, 255, 255));
                DrawTextW(memDC, g_lyrics[i].original, -1, &textRect, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

                if (g_lyrics[i].translation[0] != L'\0') {
                    SelectObject(memDC, fontActiveSub);
                    SetTextColor(memDC, RGB(29, 185, 84));
                    DrawTextW(memDC, g_lyrics[i].translation, -1, &transRect, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
                }
            } else {
                SelectObject(memDC, fontDim);
                SetTextColor(memDC, RGB(145, 152, 166));
                DrawTextW(memDC, g_lyrics[i].original, -1, &textRect, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
            }
        }
    } else {
        SelectObject(memDC, fontDim);
        SetTextColor(memDC, RGB(160, 165, 175));
        RECT waitRc = { mainLeft + 35, centerY, mainRight - 35, centerY + 40 };
        DrawTextW(memDC, g_is_fetching ? L"Syncing & Translating..." : L"No lyrics found.", -1, &waitRc, DT_LEFT | DT_SINGLELINE);
    }

    // ==========================================
    // 4. AYARLAR MODALI (GÖRSEL SEÇİM KUTULARI İLE)
    // ==========================================
    if (g_show_settings_modal) {
        RECT modalRc = { mainLeft + 25, 60, mainLeft + 490, rc.bottom - 90 };
        HBRUSH modalBrush = CreateSolidBrush(RGB(25, 28, 36));
        SelectObject(memDC, modalBrush);
        RoundRect(memDC, modalRc.left, modalRc.top, modalRc.right, modalRc.bottom, 16, 16);
        DeleteObject(modalBrush);

        SelectObject(memDC, fontTitle);
        SetTextColor(memDC, RGB(255, 255, 255));
        RECT modalTitle = { modalRc.left + 25, modalRc.top + 15, modalRc.right - 25, modalRc.top + 45 };
        DrawTextW(memDC, g_ui_langs[g_selected_ui_lang].title_settings, -1, &modalTitle, DT_LEFT | DT_SINGLELINE);

        // Kategori 1: Arayüz Dili (My Language)
        SelectObject(memDC, fontSideText);
        SetTextColor(memDC, RGB(29, 185, 84));
        RECT uiSecTitle = { modalRc.left + 25, modalRc.top + 50, modalRc.right - 25, modalRc.top + 72 };
        DrawTextW(memDC, g_ui_langs[g_selected_ui_lang].sec_ui_lang, -1, &uiSecTitle, DT_LEFT | DT_SINGLELINE);

        for (int l = 0; l < 5; l++) {
            int bx = modalRc.left + 25 + (l % 3) * 140;
            int by = modalRc.top + 78 + (l / 3) * 40;
            RECT btnL = { bx, by, bx + 130, by + 34 };

            HBRUSH lbBrush = CreateSolidBrush(g_selected_ui_lang == l ? RGB(29, 185, 84) : RGB(38, 42, 54));
            SelectObject(memDC, lbBrush);
            RoundRect(memDC, btnL.left, btnL.top, btnL.right, btnL.bottom, 10, 10);
            DeleteObject(lbBrush);

            SelectObject(memDC, fontSmall);
            SetTextColor(memDC, g_selected_ui_lang == l ? RGB(0, 0, 0) : RGB(235, 240, 250));
            DrawTextW(memDC, g_ui_langs[l].name, -1, &btnL, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        // Kategori 2: Hedef Çeviri Dili (Learning Language)
        SelectObject(memDC, fontSideText);
        SetTextColor(memDC, RGB(29, 185, 84));
        RECT transSecTitle = { modalRc.left + 25, modalRc.top + 170, modalRc.right - 25, modalRc.top + 192 };
        DrawTextW(memDC, g_ui_langs[g_selected_ui_lang].sec_trans_lang, -1, &transSecTitle, DT_LEFT | DT_SINGLELINE);

        for (int l = 0; l < 5; l++) {
            int bx = modalRc.left + 25 + (l % 3) * 140;
            int by = modalRc.top + 198 + (l / 3) * 40;
            RECT btnL = { bx, by, bx + 130, by + 34 };

            HBRUSH lbBrush = CreateSolidBrush(g_selected_trans_lang == l ? RGB(29, 185, 84) : RGB(38, 42, 54));
            SelectObject(memDC, lbBrush);
            RoundRect(memDC, btnL.left, btnL.top, btnL.right, btnL.bottom, 10, 10);
            DeleteObject(lbBrush);

            SelectObject(memDC, fontSmall);
            SetTextColor(memDC, g_selected_trans_lang == l ? RGB(0, 0, 0) : RGB(235, 240, 250));
            DrawTextW(memDC, g_trans_names[l], -1, &btnL, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        // Kapat ve Kaydet Butonu
        RECT closeBtn = { modalRc.left + 25, modalRc.bottom - 50, modalRc.right - 25, modalRc.bottom - 15 };
        HBRUSH cbBrush = CreateSolidBrush(RGB(48, 52, 65));
        SelectObject(memDC, cbBrush);
        RoundRect(memDC, closeBtn.left, closeBtn.top, closeBtn.right, closeBtn.bottom, 12, 12);
        DeleteObject(cbBrush);

        SelectObject(memDC, fontSideBtn);
        SetTextColor(memDC, RGB(255, 255, 255));
        DrawTextW(memDC, g_ui_langs[g_selected_ui_lang].close_btn, -1, &closeBtn, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    // ==========================================
    // 5. ALT OYNATMA BARI
    // ==========================================
    RECT footerRc = { 0, rc.bottom - 90, rc.right, rc.bottom };
    HBRUSH footerBrush = CreateSolidBrush(RGB(13, 14, 18));
    FillRect(memDC, &footerRc, footerBrush);
    DeleteObject(footerBrush);

    HBRUSH artBrush = CreateSolidBrush(RGB(32, 35, 45));
    SelectObject(memDC, artBrush);
    RoundRect(memDC, 20, rc.bottom - 74, 76, rc.bottom - 18, 10, 10);
    DeleteObject(artBrush);

    SelectObject(memDC, fontTitle);
    SetTextColor(memDC, RGB(29, 185, 84));
    RECT artTextRc = { 20, rc.bottom - 74, 76, rc.bottom - 18 };
    DrawTextW(memDC, L"♪", -1, &artTextRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    SelectObject(memDC, fontSideText);
    SetTextColor(memDC, RGB(255, 255, 255));
    RECT nowSong = { 88, rc.bottom - 62, 280, rc.bottom - 42 };
    DrawTextW(memDC, g_song_title, -1, &nowSong, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);

    SelectObject(memDC, fontSmall);
    SetTextColor(memDC, RGB(160, 165, 175));
    RECT nowArtist = { 88, rc.bottom - 40, 280, rc.bottom - 20 };
    DrawTextW(memDC, g_artist_name, -1, &nowArtist, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);

    int midX = rc.right / 2;
    int btnY = rc.bottom - 72;
    HBRUSH btnBrush = CreateSolidBrush(RGB(255, 255, 255));
    SelectObject(memDC, btnBrush);
    RoundRect(memDC, midX - 16, btnY, midX + 16, btnY + 32, 32, 32);
    DeleteObject(btnBrush);

    SelectObject(memDC, fontSmall);
    SetTextColor(memDC, RGB(0, 0, 0));
    RECT btnTextRc = { midX - 16, btnY + 7, midX + 16, btnY + 27 };
    DrawTextW(memDC, g_is_playing ? L"❚❚" : L"▶", -1, &btnTextRc, DT_CENTER | DT_SINGLELINE);

    RECT rcBar;
    GetProgressBarRect(&rc, &rcBar);

    HBRUSH barBg = CreateSolidBrush(RGB(45, 48, 60));
    SelectObject(memDC, barBg);
    RoundRect(memDC, rcBar.left, rcBar.top, rcBar.right, rcBar.bottom, 4, 4);
    DeleteObject(barBg);

    int fillWidth = 0;
    if (g_total_duration_ms > 0) {
        fillWidth = (int)(((double)g_current_pos_ms / g_total_duration_ms) * (rcBar.right - rcBar.left));
        if (fillWidth > (rcBar.right - rcBar.left)) fillWidth = (rcBar.right - rcBar.left);
    }

    if (fillWidth > 0) {
        HBRUSH barFill = CreateSolidBrush(RGB(29, 185, 84));
        SelectObject(memDC, barFill);
        RoundRect(memDC, rcBar.left, rcBar.top, rcBar.left + fillWidth, rcBar.bottom, 4, 4);
        DeleteObject(barFill);
    }

    int thumbX = rcBar.left + fillWidth;
    int thumbY = rcBar.top + 2;
    HBRUSH thumbBrush = CreateSolidBrush(RGB(255, 255, 255));
    SelectObject(memDC, thumbBrush);
    RoundRect(memDC, thumbX - 5, thumbY - 5, thumbX + 5, thumbY + 5, 10, 10);
    DeleteObject(thumbBrush);

    wchar_t curTimeStr[16], totalTimeStr[16];
    FormatTime(g_current_pos_ms, curTimeStr);
    FormatTime(g_total_duration_ms > 0 ? g_total_duration_ms : 0, totalTimeStr);

    SelectObject(memDC, fontSmall);
    SetTextColor(memDC, RGB(180, 185, 195));
    RECT timeCurRect = { rcBar.left - 50, rcBar.top - 6, rcBar.left - 10, rcBar.top + 14 };
    DrawTextW(memDC, curTimeStr, -1, &timeCurRect, DT_RIGHT | DT_SINGLELINE);

    RECT timeTotalRect = { rcBar.right + 10, rcBar.top - 6, rcBar.right + 55, rcBar.top + 14 };
    DrawTextW(memDC, totalTimeStr, -1, &timeTotalRect, DT_LEFT | DT_SINGLELINE);

    RECT rcVol;
    GetVolumeBarRect(&rc, &rcVol);

    SelectObject(memDC, fontSmall);
    SetTextColor(memDC, RGB(180, 185, 195));
    RECT volLabel = { rcVol.left - 30, rcVol.top - 5, rcVol.left - 5, rcVol.bottom + 5 };
    DrawTextW(memDC, L"🔊", -1, &volLabel, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    HBRUSH volBg = CreateSolidBrush(RGB(45, 48, 60));
    SelectObject(memDC, volBg);
    RoundRect(memDC, rcVol.left, rcVol.top, rcVol.right, rcVol.bottom, 4, 4);
    DeleteObject(volBg);

    int volFill = (int)(((double)g_volume_level / 1000.0) * (rcVol.right - rcVol.left));
    if (volFill > 0) {
        HBRUSH volFillBrush = CreateSolidBrush(RGB(29, 185, 84));
        SelectObject(memDC, volFillBrush);
        RoundRect(memDC, rcVol.left, rcVol.top, rcVol.left + volFill, rcVol.bottom, 4, 4);
        DeleteObject(volFillBrush);
    }

    BitBlt(hdc, 0, 0, rc.right, rc.bottom, memDC, 0, 0, SRCCOPY);

    SelectObject(memDC, oldPen);
    DeleteObject(nullPen);
    DeleteObject(fontSideBig);
    DeleteObject(fontSideBtn);
    DeleteObject(fontSideText);
    DeleteObject(fontTitle);
    DeleteObject(fontActive);
    DeleteObject(fontActiveSub);
    DeleteObject(fontDim);
    DeleteObject(fontSmall);
    SelectObject(memDC, oldBitmap);
    DeleteObject(memBitmap);
    DeleteDC(memDC);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            HICON hAppIcon = CreateHighDefAppIcon();
            SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hAppIcon);
            SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hAppIcon);

            LoadLanguageConfig(); // Kaydedilen dilleri hafızaya yükle
            ScanPlaylist(hwnd, TRUE);

            SetTimer(hwnd, TIMER_ID, 25, NULL); 
            SetTimer(hwnd, WATCHER_TIMER_ID, 3000, NULL);
            break;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_SIZE: {
            InvalidateRect(hwnd, NULL, FALSE);
            break;
        }

        case WM_PYTHON_DONE: {
            g_is_fetching = FALSE;
            LoadLRC();

            mciSendStringW(L"stop song", NULL, 0, NULL);
            mciSendStringW(L"close song", NULL, 0, NULL);

            wchar_t openCmd[MAX_PATH + 64];
            wsprintfW(openCmd, L"open \"%s\" type mpegvideo alias song", g_active_song_path);
            mciSendStringW(openCmd, NULL, 0, NULL);
            mciSendStringW(L"set song time format ms", NULL, 0, NULL);

            SetVolume(g_volume_level);
            g_total_duration_ms = QueryMCI(L"status song length");
            mciSendStringW(L"play song", NULL, 0, NULL);
            g_is_playing = TRUE;
            g_current_index = 0;
            g_current_scroll_y = 0.0;
            g_target_scroll_y = 0.0;

            InvalidateRect(hwnd, NULL, FALSE);
            break;
        }

        case WM_TIMER: {
            if (wParam == WATCHER_TIMER_ID) {
                ScanPlaylist(hwnd, FALSE);
                return 0;
            }

            if (wParam == TIMER_ID) {
                if (g_is_playing && !g_is_dragging_progress) {
                    // GERÇEKÇİ MCI SENKRONİZASYONU VE GÜVENLİK KONTROLÜ
                    long real_pos = QueryMCI(L"status song position");
                    if (real_pos >= 0) {
                        g_current_pos_ms = real_pos;
                    } else {
                        g_current_pos_ms += 25;
                    }

                    // Şarkı bitiş kontrolü (Takılma/Sıçrama engelleme)
                    if (g_total_duration_ms > 0 && g_current_pos_ms >= g_total_duration_ms - 100) {
                        g_is_playing = FALSE;
                        g_current_pos_ms = g_total_duration_ms;
                        mciSendStringW(L"stop song", NULL, 0, NULL);
                    }

                    int new_index = 0;
                    if (g_lyric_count > 0) {
                        for (int i = 0; i < g_lyric_count; i++) {
                            if (g_current_pos_ms >= g_lyrics[i].timestamp_ms) {
                                new_index = i;
                            } else {
                                break;
                            }
                        }
                    }
                    g_current_index = new_index;
                    g_target_scroll_y = g_current_index * 66.0;
                }

                BOOL needsRepaint = FALSE;
                double diff = g_target_scroll_y - g_current_scroll_y;
                if (fabs(diff) > 0.3) {
                    g_current_scroll_y += diff * 0.25;
                    needsRepaint = TRUE;
                } else {
                    g_current_scroll_y = g_target_scroll_y;
                }

                if (g_is_playing || needsRepaint) {
                    InvalidateRect(hwnd, NULL, FALSE);
                }
            }
            break;
        }

        case WM_LBUTTONDOWN: {
            int mouseX = LOWORD(lParam);
            int mouseY = HIWORD(lParam);

            RECT rcClient, rcBar, rcVol;
            GetClientRect(hwnd, &rcClient);
            GetProgressBarRect(&rcClient, &rcBar);
            GetVolumeBarRect(&rcClient, &rcVol);

            int sideWidth = 240;
            int rightPanelWidth = 260;
            int rightPanelLeft = rcClient.right - rightPanelWidth - 8;

            // 1. Ayarlar Modalı Tıklamaları
            if (g_show_settings_modal) {
                int mainLeft = sideWidth + 8;
                RECT modalRc = { mainLeft + 25, 60, mainLeft + 490, rcClient.bottom - 90 };

                // Arayüz Dili Seçimi
                for (int l = 0; l < 5; l++) {
                    int bx = modalRc.left + 25 + (l % 3) * 140;
                    int by = modalRc.top + 78 + (l / 3) * 40;
                    RECT btnL = { bx, by, bx + 130, by + 34 };
                    POINT p = { mouseX, mouseY };
                    if (PtInRect(&btnL, p)) {
                        g_selected_ui_lang = l;
                        SaveLanguageConfig();
                        InvalidateRect(hwnd, NULL, FALSE);
                        return 0;
                    }
                }

                // Çeviri Dili Seçimi
                for (int l = 0; l < 5; l++) {
                    int bx = modalRc.left + 25 + (l % 3) * 140;
                    int by = modalRc.top + 198 + (l / 3) * 40;
                    RECT btnL = { bx, by, bx + 130, by + 34 };
                    POINT p = { mouseX, mouseY };
                    if (PtInRect(&btnL, p)) {
                        g_selected_trans_lang = l;
                        SaveLanguageConfig();
                        if (g_selected_track_idx >= 0) {
                            StartSongByIndex(hwnd, g_selected_track_idx); 
                        }
                        InvalidateRect(hwnd, NULL, FALSE);
                        return 0;
                    }
                }

                // Kapat Butonu
                RECT closeBtn = { modalRc.left + 25, modalRc.bottom - 50, modalRc.right - 25, modalRc.bottom - 15 };
                POINT pc = { mouseX, mouseY };
                if (PtInRect(&closeBtn, pc)) {
                    g_show_settings_modal = FALSE;
                    InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
                }
                return 0;
            }

            // 2. Sol Panel Butonları
            RECT setBtnRect = { 18, 62, sideWidth - 18, 108 };
            POINT ptSet = { mouseX, mouseY };
            if (PtInRect(&setBtnRect, ptSet)) {
                g_show_settings_modal = TRUE;
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }

            RECT fullBtnRect = { 18, 118, 115, 158 };
            if (PtInRect(&fullBtnRect, ptSet)) {
                ToggleFullscreen(hwnd);
                return 0;
            }

            RECT ecoBtnRect = { 122, 118, sideWidth - 18, 158 };
            if (PtInRect(&ecoBtnRect, ptSet)) {
                g_eco_mode = !g_eco_mode;
                if (g_eco_mode) {
                    ApplyWorkingSetReduction();
                }
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }

            // 3. Sağ Panel (Şarkı Listesi)
            if (mouseX >= rightPanelLeft && mouseX <= rcClient.right - 8) {
                int listTop = 75;
                int trackItemHeight = 44;
                for (int t = 0; t < g_playlist_count; t++) {
                    int itemY = listTop + t * trackItemHeight;
                    if (mouseY >= itemY && mouseY <= itemY + trackItemHeight - 4) {
                        StartSongByIndex(hwnd, t);
                        return 0;
                    }
                }
            }

            // 4. İlerleme Barı
            RECT clickArea = { rcBar.left - 8, rcBar.top - 12, rcBar.right + 8, rcBar.bottom + 12 };
            POINT pt = { mouseX, mouseY };
            if (PtInRect(&clickArea, pt)) {
                g_is_dragging_progress = TRUE;
                SetCapture(hwnd);

                double ratio = (double)(mouseX - rcBar.left) / (rcBar.right - rcBar.left);
                if (ratio < 0.0) ratio = 0.0;
                if (ratio > 1.0) ratio = 1.0;
                SeekToMs((long)(ratio * g_total_duration_ms));
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }

            // 5. Ses Barı
            RECT volArea = { rcVol.left - 5, rcVol.top - 10, rcVol.right + 5, rcVol.bottom + 10 };
            if (PtInRect(&volArea, pt)) {
                g_is_dragging_volume = TRUE;
                SetCapture(hwnd);

                double ratio = (double)(mouseX - rcVol.left) / (rcVol.right - rcVol.left);
                if (ratio < 0.0) ratio = 0.0;
                if (ratio > 1.0) ratio = 1.0;
                SetVolume((int)(ratio * 1000));
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }

            // 6. Play / Pause Butonu
            int midX = rcClient.right / 2;
            int btnY = rcClient.bottom - 72;
            RECT btnRect = { midX - 16, btnY, midX + 16, btnY + 32 };
            if (PtInRect(&btnRect, pt)) {
                if (g_is_playing) {
                    mciSendStringW(L"pause song", NULL, 0, NULL);
                    g_is_playing = FALSE;
                } else {
                    mciSendStringW(L"resume song", NULL, 0, NULL);
                    g_is_playing = TRUE;
                }
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }

            // 7. Lirik Satırına Tıklayarak Atlama
            int mainLeft = sideWidth + 8;
            int mainRight = rightPanelLeft - 8;
            if (mouseX > mainLeft && mouseX < mainRight && mouseY > 95 && mouseY < rcClient.bottom - 95 && g_lyric_count > 0) {
                int centerY = (rcClient.bottom - 95) / 2 + 10;
                int lineHeight = 66;
                int clickedIndex = (int)((mouseY - centerY + g_current_scroll_y) / (double)lineHeight);
                if (clickedIndex >= 0 && clickedIndex < g_lyric_count) {
                    SeekToMs(g_lyrics[clickedIndex].timestamp_ms);
                    g_current_index = clickedIndex;
                    InvalidateRect(hwnd, NULL, FALSE);
                }
            }
            break;
        }

        case WM_MOUSEMOVE: {
            int mouseX = LOWORD(lParam);
            RECT rcClient, rcBar, rcVol;
            GetClientRect(hwnd, &rcClient);

            if (g_is_dragging_progress) {
                GetProgressBarRect(&rcClient, &rcBar);
                double ratio = (double)(mouseX - rcBar.left) / (rcBar.right - rcBar.left);
                if (ratio < 0.0) ratio = 0.0;
                if (ratio > 1.0) ratio = 1.0;
                SeekToMs((long)(ratio * g_total_duration_ms));
                InvalidateRect(hwnd, NULL, FALSE);
            } else if (g_is_dragging_volume) {
                GetVolumeBarRect(&rcClient, &rcVol);
                double ratio = (double)(mouseX - rcVol.left) / (rcVol.right - rcVol.left);
                if (ratio < 0.0) ratio = 0.0;
                if (ratio > 1.0) ratio = 1.0;
                SetVolume((int)(ratio * 1000));
                InvalidateRect(hwnd, NULL, FALSE);
            }
            break;
        }

        case WM_LBUTTONUP: {
            if (g_is_dragging_progress || g_is_dragging_volume) {
                g_is_dragging_progress = FALSE;
                g_is_dragging_volume = FALSE;
                ReleaseCapture();
            }
            break;
        }

        case WM_KEYDOWN: {
            if (wParam == VK_SPACE) {
                if (g_is_playing) {
                    mciSendStringW(L"pause song", NULL, 0, NULL);
                    g_is_playing = FALSE;
                } else {
                    mciSendStringW(L"resume song", NULL, 0, NULL);
                    g_is_playing = TRUE;
                }
                InvalidateRect(hwnd, NULL, FALSE);
            } else if (wParam == VK_F11) {
                ToggleFullscreen(hwnd);
            } else if (wParam == VK_F2) {
                g_eco_mode = !g_eco_mode;
                if (g_eco_mode) {
                    ApplyWorkingSetReduction();
                }
                InvalidateRect(hwnd, NULL, FALSE);
            } else if (wParam == VK_LEFT) {
                SeekToMs(g_current_pos_ms - 5000);
                InvalidateRect(hwnd, NULL, FALSE);
            } else if (wParam == VK_RIGHT) {
                SeekToMs(g_current_pos_ms + 5000);
                InvalidateRect(hwnd, NULL, FALSE);
            }
            break;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RenderSpotifyDesktop(hwnd, hdc);
            EndPaint(hwnd, &ps);
            break;
        }

        case WM_DESTROY: {
            KillTimer(hwnd, TIMER_ID);
            KillTimer(hwnd, WATCHER_TIMER_ID);
            mciSendStringW(L"stop song", NULL, 0, NULL);
            mciSendStringW(L"close song", NULL, 0, NULL);
            PostQuitMessage(0);
            break;
        }

        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    typedef BOOL(WINAPI *SetProcessDpiAwarenessContextProc)(DPI_AWARENESS_CONTEXT);
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (hUser32) {
        SetProcessDpiAwarenessContextProc setDpi = 
            (SetProcessDpiAwarenessContextProc)GetProcAddress(hUser32, "SetProcessDpiAwarenessContext");
        if (setDpi) {
            setDpi(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        }
    }

    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"AuraUltimateCore";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);

    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(
        0, L"AuraUltimateCore", L"Aura — Canlı Telaffuz & Çalma Listesi Oynatıcısı",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1140, 740,
        NULL, NULL, hInstance, NULL
    );

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return (int)msg.wParam;
}