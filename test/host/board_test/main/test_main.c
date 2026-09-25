#include <stdio.h>
#include <stdlib.h>

#include "unity.h"
#include "unity_test_runner.h"

void setUp(void) {}
void tearDown(void) {}

void app_main(void)
{
    UNITY_BEGIN();
    unity_run_all_tests();
    int failures = UNITY_END();
    fflush(stdout);
    exit(failures ? 1 : 0);
}
