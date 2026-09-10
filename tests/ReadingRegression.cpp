#include "TextBook.h"
#include "EpubBook.h"
#include "Utils.h"
#include <stdio.h>
#include <stdexcept>
#include <string>

static int checks = 0;
extern Book* _Book;
extern LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

static void Check(bool ok, const char* description)
{
    ++checks;
    if (!ok)
    {
        fprintf(stderr, "FAIL: %s\n", description);
        ExitProcess(1);
    }
    printf("PASS: %s\n", description);
}

class TestTextBook : public TextBook
{
public:
    using Book::DecodeText;
    using Book::ForceKill;
    using TextBook::ReadBook;
    chapter_rule_t rule = {};

    TestTextBook() { SetChapterRule(&rule); }

    bool Chapters(const wchar_t* text, int mode, const wchar_t* pattern = L"")
    {
        ForceKill();
        CloseBook();
        m_bForceKill = false;
        m_Length = (int)wcslen(text);
        m_Text = _wcsdup(text);
        rule.rule = mode;
        wcscpy_s(rule.regex, pattern);
        wcscpy_s(rule.keyword, pattern);
        SetChapterRule(&rule);
        return ParserChapters() != FALSE;
    }

    bool LoadBytes(const char* bytes, int size)
    {
        char* owned = (char*)malloc(size ? size : 1);
        memcpy(owned, bytes, size);
        return OpenBook(owned, size, NULL) != FALSE;
    }

    bool Wait(DWORD timeout = 3000)
    {
        return m_hThread && WaitForSingleObject(m_hThread, timeout) == WAIT_OBJECT_0;
    }
};

class ControlledBook : public TextBook
{
public:
    enum Mode { Immediate, UntilCancelled, Throw };
    Mode mode;
    HANDLE entered = CreateEvent(NULL, TRUE, FALSE, NULL);
    std::atomic<int> calls{0};

    explicit ControlledBook(Mode value) : mode(value) {}
    ~ControlledBook() { ForceKill(); CloseHandle(entered); }
    void Cancel() { ForceKill(); }
    bool Wait(DWORD timeout = 3000)
    {
        return m_hThread && WaitForSingleObject(m_hThread, timeout) == WAIT_OBJECT_0;
    }
protected:
    BOOL ParserBook(HWND) override
    {
        ++calls;
        SetEvent(entered);
        if (mode == Throw)
            throw std::runtime_error("parser failure");
        if (mode == UntilCancelled)
            while (!m_bForceKill)
                SwitchToThread();
        return !m_bForceKill;
    }
};

class TestEpubBook : public EpubBook
{
public:
    bool LoadChapters()
    {
        const char* pages[] = {
            "<html><head><title>First</title></head><body><p>First body</p></body></html>",
            "<html><head></head><body><p>Second body</p></body></html>",
            "<html><head><title></title></head><body><p>Third body</p></body></html>"
        };
        epub_t epub;
        manifest_t manifests[3];
        for (int i = 0; i < 3; ++i)
        {
            std::string id = std::to_string(i);
            manifests[i].href = id;
            epub.manifests[id] = &manifests[i];
            epub.spines.push_back(id);
            file_data_t data = {_strdup(pages[i]), (int)strlen(pages[i])};
            m_flist[id] = data;
        }
        return ParserChapters(epub) != FALSE;
    }
};

class RegexCancellationBook : public TextBook
{
public:
    HANDLE entered = CreateEvent(NULL, TRUE, FALSE, NULL);
    bool result = true;
    RegexCancellationBook()
    {
        chapter_rule_t rule = {};
        rule.rule = 2;
        wcscpy_s(rule.regex, L"not-present-in-this-book");
        SetChapterRule(&rule);
        m_Length = 16 * 1024 * 1024;
        m_Text = (wchar_t*)malloc(((size_t)m_Length + 1) * sizeof(wchar_t));
        wmemset(m_Text, L'a', m_Length);
        m_Text[m_Length] = 0;
    }
    ~RegexCancellationBook() { ForceKill(); CloseHandle(entered); }
    void Cancel() { ForceKill(); }
protected:
    BOOL ParserBook(HWND) override
    {
        SetEvent(entered);
        result = ParserChaptersRegex() != FALSE;
        return result;
    }
};

static void DecodeCases()
{
    TestTextBook book;
    struct Sample { const char* data; int size; const wchar_t* expected; } samples[] = {
        {"hello\r\nworld", 12, L"hello\nworld"},
        {"\xef\xbb\xbf\xe4\xb8\xad\xe6\x96\x87", 9, L"中文"},
        {"\xff\xfe\x2d\x4e\x87\x65", 6, L"中文"},
        {"\xfe\xff\x4e\x2d\x65\x87", 6, L"中文"},
        {"", 0, L""},
        {"\xff\xfe", 2, L""},
        {"\xef\xbb\xbf", 3, L""},
    };
    for (const auto& sample : samples)
    {
        wchar_t* text = NULL;
        int length = 0;
        Check(book.DecodeText(sample.data, sample.size, &text, &length)
            && text && wcscmp(text, sample.expected) == 0
            && length == (int)wcslen(sample.expected), "text decoding and normalization");
        free(text);
    }
    for (const char* bytes : {"\xff\xfe\x41", "\xfe\xff\x41"})
    {
        wchar_t* text = NULL;
        int length = 0;
        Check(!book.DecodeText(bytes, 3, &text, &length) && !text && length == 0,
            "odd UTF-16 byte count is rejected safely");
    }
    wchar_t* text = NULL;
    int length = 0;
    Check(!book.DecodeText(NULL, 8, &text, &length), "null input is rejected");
    Check(!book.DecodeText("a", -1, &text, &length), "negative input length is rejected");
    Check(!book.DecodeText("\xff\xfe\0\0", 4, &text, &length), "unsupported UTF-32 is rejected");
    Check(!utf8_to_utf16(NULL, 1, &length) && length == 0, "UTF-8 conversion rejects null input");
    Check(!ansi_to_utf16("x", -1, &length) && length == 0, "ANSI conversion rejects invalid length");
    if (GetACP() == 936)
    {
        text = ansi_to_utf16("\xd6\xd0\xce\xc4", 4, &length);
        Check(text && wcscmp(text, L"中文") == 0, "GBK text decoding");
        free(text);
    }
}

static void ChapterCases()
{
    TestTextBook book;
    for (const wchar_t* pattern : {L"", L"^", L"$", L"(?=a)", L"b*"})
        Check(book.Chapters(L"aaa", 2, pattern) && book.GetChapters()->empty(),
            "empty-only regex matches terminate without fake chapters");
    Check(book.Chapters(L"aaa", 2, L"a*") && book.GetChapters()->size() == 1,
        "nullable regex keeps real nonempty matches");
    Check(book.Chapters(L"Chapter 1\ntext\nChapter 2", 2, L"Chapter [0-9]+")
        && book.GetChapters()->size() == 2 && book.GetChapters()->at(1).index == 15,
        "regex chapter offsets are preserved");
    Check(!book.Chapters(L"abc", 2, L"["), "invalid regex returns failure");
    Check(!book.Chapters(std::wstring(2000, L'a').c_str(), 2, L"(a+)+b"),
        "regex complexity/stack exception returns failure");
    Check(book.Chapters(L"第一章\n内容\n第二章", 0) && book.GetChapters()->size() == 2,
        "default chapters handle final line without newline");
    Check(book.Chapters(L"\r\nChapter 1\r\nabc\r\nChapter 2", 1, L"Chapter")
        && book.GetChapters()->size() == 2 && book.GetChapters()->at(1).index == 18,
        "keyword chapters respect CRLF lengths");
    Check(book.Chapters(L"xxa", 1, L"abc") && book.GetChapters()->empty(),
        "keyword scan does not read beyond final line");
    Check(book.Chapters(L"", 0) && book.GetChapters()->empty(), "empty chapter input");
}

static void ThreadCases(HWND window)
{
    DWORD before = 0, after = 0;
    GetProcessHandleCount(GetCurrentProcess(), &before);
    bool ok = true;
    for (int i = 0; i < 300; ++i)
    {
        ControlledBook book(ControlledBook::Immediate);
        ok = ok && book.OpenBook(NULL) && book.Wait();
        book.Cancel();
        ok = ok && !book.IsLoading();
    }
    GetProcessHandleCount(GetCurrentProcess(), &after);
    Check(ok && after <= before + 2, "300 fast loads finish without leaking thread handles");

    ULONGLONG start = GetTickCount64();
    for (int i = 0; i < 100; ++i)
    {
        ControlledBook book(ControlledBook::UntilCancelled);
        Check(book.OpenBook(NULL), "start cancellable load");
        // Includes cancellation before the worker has started executing.
        if (i % 2 == 0)
            Check(WaitForSingleObject(book.entered, 3000) == WAIT_OBJECT_0, "worker entered parser");
        book.Cancel();
        if (book.IsLoading())
            ok = false;
    }
    Check(ok && GetTickCount64() - start < 5000, "100 immediate/in-flight cancellations avoid old 5-second stall");

    ControlledBook failure(ControlledBook::Throw);
    Check(failure.OpenBook(window) && failure.Wait(), "parser exception does not kill process");
    MSG message = {};
    Check(PeekMessage(&message, window, WM_OPEN_BOOK, WM_OPEN_BOOK, PM_REMOVE)
        && message.wParam == 0 && (ULONG_PTR)message.lParam == failure.GetLoadId(),
        "parser failure notification includes request identity");

    ControlledBook book(ControlledBook::Immediate);
    Check(book.OpenBook(window) && book.Wait(), "first completion queued");
    ULONG_PTR oldId = book.GetLoadId();
    Check(book.OpenBook(window) && book.Wait() && oldId != book.GetLoadId(), "reopen generates a new request identity");
    Check(PeekMessage(&message, window, WM_OPEN_BOOK, WM_OPEN_BOOK, PM_REMOVE)
        && (ULONG_PTR)message.lParam == oldId && (ULONG_PTR)message.lParam != book.GetLoadId(),
        "old completion remains distinguishable after reopening");
    _Book = &book;
    WndProc(window, WM_OPEN_BOOK, 0, (LPARAM)oldId);
    Check(_Book == &book, "actual window handler ignores stale failure instead of deleting current book");
    _Book = NULL;
    Check(PeekMessage(&message, window, WM_OPEN_BOOK, WM_OPEN_BOOK, PM_REMOVE)
        && (ULONG_PTR)message.lParam == book.GetLoadId(), "current completion has current identity");

    TestTextBook textBook;
    Check(textBook.LoadBytes("Chapter 1\nhello", 15) && textBook.Wait()
        && textBook.GetTextLength() > 0, "real async TXT memory load");
    textBook.ForceKill();

    std::string largeText(8 * 1024 * 1024, 'a');
    TestTextBook longBook;
    Check(longBook.LoadBytes(largeText.data(), (int)largeText.size()), "start real long-line TXT load");
    start = GetTickCount64();
    longBook.ForceKill();
    Check(!longBook.IsLoading() && GetTickCount64() - start < 3000, "cancel real long-line TXT load");

    RegexCancellationBook regexBook;
    Check(regexBook.OpenBook(window)
        && WaitForSingleObject(regexBook.entered, 3000) == WAIT_OBJECT_0, "start real long regex search");
    Sleep(20);
    Check(regexBook.IsLoading(), "regex is still scanning before cancellation");
    start = GetTickCount64();
    regexBook.Cancel();
    Check(!regexBook.IsLoading() && !regexBook.result && GetTickCount64() - start < 1000,
        "interrupt regex before its first match without killing the thread");
    Check(!PeekMessage(&message, window, WM_OPEN_BOOK, WM_OPEN_BOOK, PM_REMOVE),
        "cancelled regex load sends no stale completion");
}

static void FileCases()
{
    wchar_t directory[MAX_PATH] = {}, path[MAX_PATH] = {};
    GetCurrentDirectory(MAX_PATH, directory);
    Check(GetTempFileName(directory, L"rsh", 0, path) != 0, "create isolated file fixture");
    FILE* file = _wfopen(path, L"wb");
    Check(file != NULL, "open fixture");
    fwrite("Chapter 1\nhello", 1, 15, file);
    fclose(file);
    {
        TestTextBook book;
        book.SetFileName(path);
        Check(book.OpenBook(NULL) && book.Wait() && book.GetTextLength() == 15,
            "real async TXT disk load");
    }
    file = _wfopen(path, L"wb");
    fclose(file);
    {
        TestTextBook book;
        book.SetFileName(path);
        Check(!book.ReadBook(), "empty disk file fails without crashing");
    }
    DeleteFile(path);
    {
        TestTextBook book;
        book.SetFileName(path);
        Check(!book.ReadBook(), "missing disk file fails without crashing");
    }
}

int main()
{
    HWND window = CreateWindowEx(0, L"STATIC", L"Rish regression messages", 0,
        0, 0, 0, 0, HWND_MESSAGE, NULL, GetModuleHandle(NULL), NULL);
    Check(window != NULL, "message-only test window");
    DecodeCases();
    ChapterCases();
    FileCases();
    ThreadCases(window);
    {
        TestEpubBook epub;
        Check(epub.LoadChapters() && epub.GetTextLength() > 0
            && epub.GetChapters()->size() == 1, "EPUB handles missing/empty titles after a titled chapter");
    }
    DestroyWindow(window);
    printf("All %d checks passed.\n", checks);
    return 0;
}
