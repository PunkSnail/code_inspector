
#ifndef CODE_INSPECTOR_HH
#define CODE_INSPECTOR_HH

#include <string>

/* 10M code file size limit */
#define MAXIMUM_FILE_SIZE   10485760

__attribute__((unused))
static const int g_multi_num_arr[] = {
    2, 4, 8, 16
};

__attribute__((unused))
static const char *g_ignore_arr[] = {
    "prefetch",
    "PREFETCH"
};

struct format_item_t
{
    size_t size();
    void clear();

    std::string line;
    int refer_count;
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


#endif /* CODE_INSPECTOR_HH */
