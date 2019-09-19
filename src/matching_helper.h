
#ifndef MATCHING_HELPER_HH
#define MATCHING_HELPER_HH

/* don't care about capital (c >='A' && c <= 'Z') */
#define IS_VAR(c) \
    (((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_') ? \
     true : false)

#define IN_ALPHABET(c) \
    (((c >= 'a' && c <= 'z') || (c >='A' && c <= 'Z')) ? true : false)

/* match multi-packet processing lines 
 * based on single-packet processing lines
 * return:   match true     mismatch false  */
bool varied_matching_rules(const char *single, const char *multi, int n);


#endif /* MATCHING_HELPER_HH */
