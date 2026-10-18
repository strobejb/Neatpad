//
//	Run the tests: every group, or the groups named on the command line -
//	sequence, lines, chars, document or textview
//
#include "tests.h"
#ifdef _MSC_VER
#include <crtdbg.h>
#include <stdlib.h>
#endif

int g_failures = 0;

void check(bool condition, const char *expr, const char *file, int line)
{
    if(!condition)
    {
        printf("%s(%d): check failed: %s\n", file, line, expr);
        g_failures++;
    }
}

namespace
{
const test_group *const groups[] =
{
    &sequence_tests, &line_tests, &char_tests, &document_tests, &textview_tests
};

// Whether the command line asks for a group: every group, if it names none
bool wanted(const test_group *group, int argc, char *argv[])
{
    if(argc < 2)
        return true;

    for(int i = 1; i < argc; i++)
    {
        if(strcmp(argv[i], group->name) == 0)
            return true;
    }

    return false;
}
}

int main(int argc, char *argv[])
{
    // A failed native test must report to the console, never a modal CRT box.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    setvbuf(stdout, 0, _IONBF, 0);
#ifdef _MSC_VER
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
#endif
    int run = 0;

    for(size_t g = 0; g < sizeof(groups) / sizeof(groups[0]); g++)
    {
        if(!wanted(groups[g], argc, argv))
            continue;

        for(size_t i = 0; i < groups[g]->count; i++, run++)
        {
            printf("run %s\n", groups[g]->tests[i].name);
            int before = g_failures;
            groups[g]->tests[i].run();
            printf("%s %s\n", before == g_failures ? "ok" : "FAIL", groups[g]->tests[i].name);
        }
    }

    if(run == 0)
    {
        printf("No test group has that name\n");
        return 1;
    }

    if(g_failures)
    {
        printf("%d check(s) failed\n", g_failures);
        return 1;
    }

    printf("All tests passed\n");
    return 0;
}
