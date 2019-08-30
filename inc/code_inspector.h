
#ifndef CODE_INSPECTOR_HH
#define CODE_INSPECTOR_HH

#ifdef __cplusplus
extern "C" {
#endif

/* 10M code file size limit */
#define MAXIMUM_FILE_SIZE   10485760

__attribute__((unused))
static const int g_multi_no_arr[] = { 
    2, 4, 8, 16
};

__attribute__((unused))
static const char *g_ignore_arr[] = {
    "prefetch",
    "PREFETCH"
};

/* only care about 4 cases: 2, 4, 8, 16 */
typedef enum
{
    MULTI_FLOW_2 = 0,
    MULTI_FLOW_4,
    MULTI_FLOW_8,
    MULTI_FLOW_16,
    MULTI_FLOW_ARR_SIZE,

}multi_type_t;

/* check the multi-packet processing flow in code
 * return:   succ 0     fail -1 */
int code_inspector_input(const char *code_path);

#ifdef __cplusplus
}
#endif

#endif /* CODE_INSPECTOR_HH */
