//
//	Run the tests: every group, or the groups named on the command line -
//	sequence, lines, chars, document or textview
//
#include "tests.h"

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
    int run = 0;

    for(size_t g = 0; g < sizeof(groups) / sizeof(groups[0]); g++)
    {
        if(!wanted(groups[g], argc, argv))
            continue;

        for(size_t i = 0; i < groups[g]->count; i++, run++)
        {
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
