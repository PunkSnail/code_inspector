
#ifndef MATCHING_HELPER_HH
#define MATCHING_HELPER_HH

#include "code_inspector.h" /* format_item_t */

/* don't care about capital (c >='A' && c <= 'Z') */
#define IS_VAR(c) \
    (((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_') ? \
     true : false)

#define IN_ALPHABET(c) \
    (((c >= 'a' && c <= 'z') || (c >='A' && c <= 'Z')) ? true : false)

/* note: match multi-packet processing lines based on single-packet processing
 * when this function is called, the string has been formatted
 * ' ', '\r' and '\n' are removed
 *
 * return:   match true     mismatch false */
bool varied_matching_rules(const format_item_t *single,
                           const char *multi, int n);


#endif /* MATCHING_HELPER_HH */
