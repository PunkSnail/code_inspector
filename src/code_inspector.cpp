
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <unistd.h>     /* access ... */
#include <sys/stat.h>   /* stat */
#include <regex.h>      /* regcomp ... */

#include <vector>
#include <list>
#include <map>
#include <string>
#include <fstream>      /* filebuf ... */
#include <iostream>

#include "code_inspector.h"
#include "matching_helper.h"

using namespace std;

/* Don't care about spaces, the format function will remove them */
#define NORMAL_PROCESS_REG      "while\\(."

#define show(...) printf(__VA_ARGS__);

#define show_red(fmt, args...) \
{ \
    printf("\033[1;31;1m" fmt "\033[m",  ##args); \
}

#define show_green(fmt, args...) \
{ \
    printf("\033[1;32;1m" fmt "\033[m",  ##args); \
}


typedef struct
{
    uint32_t arr[MULTI_FLOW_ARR_SIZE];

}multi_start_t;

typedef struct
{
    uint32_t s_start;
    uint32_t m_start;
    uint32_t s_left;
    uint32_t m_left;
    uint32_t s_right;
    uint32_t m_right;
    multi_start_t multi_record;

}loop_helper_t;

struct code_inspector_t
{
public:
    code_inspector_t();
    ~code_inspector_t();

    void clear_refers_arr(void);

    bool is_perfect_match;
    vector <string> code_vec;
    vector <string> format_code_vec;

    /* start and end line numbers */
    list < pair<uint32_t, uint32_t> > sr_list; /* single processing range */
    list < pair<uint32_t, uint32_t> > mr_list; /* multi processing range */

    /* multi start related to single start */
    map <uint32_t, multi_start_t> related_map; 

    /* used to record match reference count */
    uint32_t *refers_arr;
    uint32_t arr_size;
};

code_inspector_t::code_inspector_t()
{
    this->is_perfect_match = false;
    /* align index and line number */
    this->code_vec.push_back("stub");

    this->refers_arr = NULL;
    this->arr_size = 0;
}

code_inspector_t::~code_inspector_t()
{
    if (this->refers_arr)
    {
        delete this->refers_arr;
        this->refers_arr = NULL;
        this->arr_size = 0;
    }
}

void code_inspector_t::clear_refers_arr(void)
{
    memset(this->refers_arr, 0, this->arr_size * sizeof(uint32_t));
}

static bool is_invalid_param(const char *code_path)
{
    bool result = true;
    struct stat st;

    if (NULL == code_path)
    {
        show_red("the code path is empty\n");
        return result;
    }
    if (access(code_path, F_OK) == -1)
    {
        show_red("access code file failed: %s\n", code_path);
        return result;
    }
    stat(code_path, &st);
    if (S_ISDIR(st.st_mode))
    {
        show_red("the code path is a directory: %s\n", code_path);
        return result;
    }
    if (st.st_size > MAXIMUM_FILE_SIZE)
    {
        show_red("code file too big, current limit: %u\n", MAXIMUM_FILE_SIZE);
        return result;
    }
    result = false;

    return result;
}

static int load_code_file(const char *open_path, code_inspector_t *p_coder)
{
    int result = -1;
    filebuf fb;
    string line;

    if (fb.open(open_path, ios::in) == NULL)
    {
        show_red("open error: %s\n", strerror(errno));
        return result;
    }
    istream file_stream(&fb);

    while (getline(file_stream, line, '\n'))
    {
        p_coder->code_vec.push_back(line);
    }
    fb.close();

    result = 0;
    return result;
}

/* when using functions of the string class,
 * don't need to care about crossing the boundary */
static void ignore_comments(string *p_line, bool &is_comment, 
                            bool &is_macro, int &if_count)
{
    /* first deal with comment symbol */
    if (p_line->compare(0, 2, "//") == 0)
    {
        p_line->clear();
        return;
    }
    /* beginning of comment */
    if (false == is_comment && p_line->compare(0, 2, "/*") == 0)
    {
        is_comment = true;
    }
    /* comment between symbols */
    if (is_comment && p_line->find("*/") == string::npos)
    {
        p_line->clear();
        return;
    }
    /* clear lines ending with comment symbol */
    if (is_comment && p_line->size() >= 2 
        && '*' == p_line->c_str()[p_line->size() - 2] 
        && '/' == p_line->c_str()[p_line->size() - 1])
    {
        p_line->clear();
    }
    is_comment = false;

    /* deal with nested #if */
    if (false == is_macro && p_line->compare(0, 4, "#if0") == 0)
    {
        is_macro = true; 
    }
    if (is_macro)
    {
        if (p_line->compare(0, 3, "#if") == 0)
        {
            if_count++;
        }
        if (p_line->compare(0, 6, "#endif") == 0 && --if_count == 0)
        {
            is_macro = false;
        }
        p_line->clear();
    }
}

static void ignore_extra(vector<string> &format_code_vec)
{
    string *p_line;
    bool is_unassigned;

    for (size_t i = 1; i < format_code_vec.size(); i++)
    {
        p_line = &format_code_vec[i];
        is_unassigned = true;

        if (p_line->empty()) {
            continue;
        }
        for (size_t j = 0; is_unassigned && j < p_line->size(); j++)
        {
            char ch = p_line->c_str()[j];

            if (!IS_VAR(ch) && ',' != ch && '*' != ch)
            {
                is_unassigned = false;
            }
        }
        /* ignore unassigned variables */
        if (is_unassigned
            && p_line->compare(0, 4, "goto")
            && p_line->compare(0, 4, "else")
            && p_line->compare(0, 5, "break")
            && p_line->compare(0, 6, "return")
            && p_line->compare(0, 8, "continue"))
        {
            p_line->clear();
            continue;
        }
        for (size_t j = 0; j < sizeof(g_ignore_arr) / sizeof(char*); j++)
        {
            if (p_line->find(g_ignore_arr[j]) != string::npos)
                p_line->clear();
        }
    }
}

static void format_code_string(vector<string> &format_code_vec)
{
    string *p_line;
    size_t i;

    bool is_comment = false;
    bool is_macro = false;
    int if_count = 0;   /* between #if 0  and #endif */

    /* "line_no + 1" exists inside the loop, so here size() - 1 */
    for (size_t line_no = 1; line_no < format_code_vec.size() - 1; line_no++)
    {
        p_line = &format_code_vec[line_no];
re_loop:
        /* must remove spaces first */
        for (i = 0; i < p_line->size(); )
        {
            char ch = p_line->c_str()[i];

            if (' ' ==  ch || ';' == ch || '\r' == ch || '\n' == ch)
            {
                p_line->erase(i, 1);
                continue;
            }
            i++;
        }
        /* end whit '=' or can't find match ')',  merge next line */
        if (i && ('=' == p_line->c_str()[i - 1] ||
                  (p_line->find('(') != string::npos
                   && p_line->find(')') == string::npos)))
        {
            *p_line += format_code_vec[line_no + 1];
            format_code_vec[line_no + 1].clear();

            line_no++;
            goto re_loop;
        }
        ignore_comments(p_line, is_comment, is_macro, if_count);
    }
    ignore_extra(format_code_vec);
}

static bool match_assign_range(list < pair<uint32_t, uint32_t> > &range_list, 
                               uint32_t first_brace, uint32_t last_brace)
{
    for (auto it = range_list.begin(); it != range_list.end(); it++)
    {
        /* line number minus line number, fuzzy matching 0, 1, 2 */
        if (first_brace - it->first < 3)
        {
            it->second = last_brace;
            return true;
        }
    }
    return false;
}

static bool reg_judge_format(const char *pattern, const char *source)
{
    bool result = false;

    regex_t regex;
    memset(&regex, 0, sizeof(regex_t));

    if (regcomp(&regex, pattern, REG_EXTENDED) != 0)
    {
        show_red("regcomp error: %s\n", pattern);
        return result;
    }
    if (regexec(&regex, source, 0, NULL, 0) == 0)
    {
        result = true;
    }
    regfree(&regex);
    
    return result;
}

static bool filter_multi_flow(string *p_line, uint32_t line_no, uint32_t *arr)
{
    /* the order is important */
    if (p_line->find('2') != string::npos)
    {
        arr[MULTI_FLOW_2] = line_no;
    }
    else if (p_line->find('4') != string::npos)
    {
        arr[MULTI_FLOW_4] = line_no;
    }
    else if (p_line->find('8') != string::npos)
    {
        arr[MULTI_FLOW_8] = line_no;
    }
    else if (p_line->find("16") != string::npos)
    {
        arr[MULTI_FLOW_16] = line_no;
    }
    else {
        return false;
    } 
    return true;
}

static void deal_with_line(code_inspector_t *p_coder, 
                           string *p_line, uint32_t i, loop_helper_t *p_h)
{
    if (p_line->find('{') != string::npos)
    {
        p_h->s_left++;
        p_h->m_left++;
    }
    if (p_line->find('}') != string::npos)
    {
        p_h->s_right++;
        p_h->m_right++;
    }
    /* assign value to the end line number */
    if (p_h->s_start && p_h->s_right == p_h->s_left)
    {
        match_assign_range(p_coder->sr_list, p_h->s_start, i);
        p_h->s_start = 0;
    }
    if (p_h->m_start && p_h->m_right == p_h->m_left)
    {
        match_assign_range(p_coder->mr_list, p_h->m_start, i);
        p_h->m_start = 0;
    }
    if (false == reg_judge_format(NORMAL_PROCESS_REG, p_line->c_str()))
    {
        return;
    }
    /* multiple process */
    if (p_line->find('0') == string::npos)
    {
        if (filter_multi_flow(p_line, i, p_h->multi_record.arr))
        {
            /* don't know the end line number yet */
            p_coder->mr_list.push_back(make_pair(i, 0));
            p_h->m_start = i;
            p_h->m_left = p_h->m_right = 0;
        }
    }
    /* single process */
    else //if (reg_judge_format(SINGLE_PROCESS_REG, p_line->c_str()))
    {
        p_coder->sr_list.push_back(make_pair(i, 0));
        p_coder->related_map.insert(make_pair(i, p_h->multi_record));

        p_h->s_start = i;
        p_h->s_left = p_h->s_right = 0;
        memset(&p_h->multi_record, 0, sizeof(multi_start_t));
    }
}

static bool matching_multi(vector <string> &format_code_vec, 
                           list < pair<uint32_t, uint32_t> > &mr_list, 
                           uint32_t line_no)
{
    uint32_t last_brace = 0;
    uint32_t left = 0;
    uint32_t right = 0;
 
    for (uint32_t i = line_no; i > 0; i--)
    {
        if (0 == format_code_vec[i].size()) {
            continue;
        }
        if (format_code_vec[i].find('}') != string::npos)
        {
            if (0 == last_brace)
            {
                last_brace = i;
            }
            right++;
        }
        if (format_code_vec[i].find('{') != string::npos 
            && ++left == right && last_brace)
        {
            if (match_assign_range(mr_list, i, last_brace))
            {
                return true;
            }
            break;  /* only once */
        }
    }
    return false;
}

static int find_key_lines(code_inspector_t *p_coder)
{
    int result = 0;

    vector <string> &format_code_vec = p_coder->format_code_vec;

    loop_helper_t helper;
    memset(&helper, 0, sizeof(loop_helper_t));

    for (uint32_t i = 0; i < (uint32_t)format_code_vec.size(); i++)
    {
        if (0 == format_code_vec[i].size()) {
            continue;
        }
        deal_with_line(p_coder, &format_code_vec[i], i, &helper);
    }
    /* erase those invalid range */
    for (auto it = p_coder->sr_list.begin(); it != p_coder->sr_list.end(); )
    {
        if (0 == it->second ||
            !matching_multi(format_code_vec, p_coder->mr_list, it->first))
        {
            /* don't worry, you can always find it in this case. */
            p_coder->related_map.erase(p_coder->related_map.find(it->first));

            p_coder->sr_list.erase(it++);
        }
        else {
            it++;
        }
    }
    if (p_coder->mr_list.empty() || p_coder->sr_list.empty())
    {
        show_red("the specified line couldn't be found\n");
    }
    return result;
}

static void compare_with_single(code_inspector_t *p_coder, multi_type_t type,
                                pair<uint32_t, uint32_t> *s_range, 
                                pair<uint32_t, uint32_t> *m_range)
{
    string *single;
    string *multi;
    bool is_match = false;
    int n = g_multi_num_arr[type];

    /* range start +1 skip the "while" line */
    for (uint32_t i = m_range->first + 1; i < m_range->second; i++)
    {
        multi = &p_coder->format_code_vec[i];
        /* ignore empty line or symbol */
        if (multi->size() < 2) {
            continue;
        }
        for (uint32_t j = s_range->first + 1; j < s_range->second; j++)
        {
            single = &p_coder->format_code_vec[j];

            /* this line has been matched multiple times or invalid length */
            if (p_coder->refers_arr[j] >= (uint32_t)n 
                || single->size() < 2 || multi->size() < single->size())
            {
                continue;
            }
            if (varied_matching_rules(single->c_str(), multi->c_str(), n))
            {
                is_match = true;
                p_coder->refers_arr[j]++;
                break;
            }
        }
        if (false == is_match)
        {
            p_coder->is_perfect_match = false;
            show_red("%u%s\n", i, p_coder->code_vec[i].c_str());
        }
        is_match = false;
    }
}

static void pick_multi_to_compare(code_inspector_t *p_coder, 
                                  multi_type_t type, uint32_t start, 
                                  pair<uint32_t, uint32_t> *s_range)
{
    for (auto multi_it = p_coder->mr_list.begin(); 
         start && multi_it != p_coder->mr_list.end(); multi_it++)
    {
        if (multi_it->first != start) {
            continue;
        }
        show_green("\nMULTI FLOW %d START %u\n", g_multi_num_arr[type], start);
        p_coder->clear_refers_arr();

        compare_with_single(p_coder, type, s_range, &(*multi_it));

        if (p_coder->is_perfect_match)
        {
            show_green("\tMULTI FLOW IS PERFECT MATCH\n");
        }
        break;
    }
}

static void code_flow_analysis(code_inspector_t *p_coder)
{
    string split(80, '/');
    string *single;

    for (auto single_it = p_coder->sr_list.begin(); 
         single_it != p_coder->sr_list.end(); single_it++)
    {
        auto map_it = p_coder->related_map.find(single_it->first);

        show("\n%s\n", split.c_str());
        for (int i = 0; i < MULTI_FLOW_ARR_SIZE; i++)
        {
            p_coder->is_perfect_match = true;

            pick_multi_to_compare(p_coder, (multi_type_t)i, 
                                  map_it->second.arr[i], &(*single_it));
        }
        /* range start +1 skip the "while" line */
        for (uint32_t i = single_it->first + 1, flag = true; 
             i < single_it->second; i++)
        {
            single = &p_coder->format_code_vec[i];

            /* this line has been matched multiple times or invalid length */
            if (p_coder->refers_arr[i] || single->size() < 2) {
                continue;
            }
            if (flag)
            {
                show_green("\nSINGLE FLOW START %u\n", single_it->first);
                flag = false;
            }
            show("%u%s\n", i, p_coder->code_vec[i].c_str());
        }
    }
}

static int inspector_start_work(code_inspector_t *p_coder)
{
    int result = -1;
    p_coder->format_code_vec = p_coder->code_vec;
    
    format_code_string(p_coder->format_code_vec);

    result = find_key_lines(p_coder);

    if (0 != result)
    {
        return result;
    }
    code_flow_analysis(p_coder);
    
    return result;
}

int code_inspector_input(const char *code_path)
{
    int result = -1;
    code_inspector_t *p_coder = NULL;

    if (is_invalid_param(code_path)) {
        goto done;
    }
    show("code path: ");
    show_green("%s\n", code_path);

    p_coder = new code_inspector_t;

    if (0 != load_code_file(code_path, p_coder))
    {
        show_red("error reading file into memory: %s\n", strerror(errno));
        goto done;
    }
    p_coder->arr_size = (uint32_t)p_coder->code_vec.size();
    p_coder->refers_arr = new uint32_t[p_coder->arr_size];

    p_coder->clear_refers_arr();

    result = inspector_start_work(p_coder);
done:
    if (p_coder)
    {
        delete p_coder;
        p_coder = NULL;
    }
    return result;
}

