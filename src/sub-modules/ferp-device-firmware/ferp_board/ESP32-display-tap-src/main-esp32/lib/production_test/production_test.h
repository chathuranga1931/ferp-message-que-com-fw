#ifndef PRODUCTION_TEST_H_
#define PRODUCTION_TEST_H_

// #define WIFI_SSID "TP-Link_4F69"
// #define WIFI_PASS "18182521"
#define WIFI_SSID "FERP-SSID"
#define WIFI_PASS "FERP-PASSWORD"

#define MAX_TEST_RESULTS 32
#define TEST_NAME_LEN    32
#define TEST_DETAIL_LEN  26

typedef enum {
    TEST_RESULT_PASS = 0,
    TEST_RESULT_FAIL,
    TEST_RESULT_SKIP,
} test_result_status_t;

typedef struct {
    char name[TEST_NAME_LEN];
    test_result_status_t status;
    char detail[TEST_DETAIL_LEN];
} test_result_t;

#ifdef __cplusplus
extern "C" {
#endif

void productionTest();
void i2c_scanner();

#ifdef __cplusplus
}
#endif

#endif /* PRODUCTION_TEST_H_ */