/* Small read-only JSON token walker for the device-management example.
 * Reads SDK-generated JSON; applications may use their existing JSON library.
 * Returns slices, never borrows a string beyond the original buffer lifetime. */
#ifndef CXWL_SAMPLE_JSON_H
#define CXWL_SAMPLE_JSON_H
#include <stddef.h>
#include <string.h>
#include <ctype.h>
static const char *sample_json_space(const char *p) { while (*p && isspace((unsigned char)*p)) ++p; return p; }
static const char *sample_json_skip(const char *p, unsigned nesting)
{
    p=sample_json_space(p);
    if(nesting>32 || !*p) return NULL;
    if(*p=='"') {
        ++p;
        while(*p) {
            if(*p=='"') return p+1;
            if(*p=='\\') { ++p; if(!*p) return NULL; }
            ++p;
        }
        return NULL;
    }
    if(*p=='{' || *p=='[') {
        const char end=*p=='{'?'}':']'; ++p;
        while(*(p=sample_json_space(p)) && *p!=end) {
            if(*p==',' || *p==':') { ++p; continue; }
            p=sample_json_skip(p,nesting+1); if(!p) return NULL;
        }
        return *p==end?p+1:NULL;
    }
    const char *start=p;
    while(*p && !isspace((unsigned char)*p) && *p!=',' && *p!='}' && *p!=']') ++p;
    return p==start?NULL:p;
}
static const char *sample_json_member(const char *object,const char *name)
{
    if(!object || *(object=sample_json_space(object))!='{') return NULL;
    const size_t length=strlen(name);
    const char *p=object+1;
    while(*(p=sample_json_space(p)) && *p!='}') {
        if(*p!='"') return NULL;
        const char *end=sample_json_skip(p,0); if(!end) return NULL;
        const int match=(size_t)(end-p)==length+2 && !memcmp(p+1,name,length);
        p=sample_json_space(end); if(*p++!=':') return NULL;
        p=sample_json_space(p);
        if(match) return p;
        p=sample_json_skip(p,0); if(!p) return NULL;
        p=sample_json_space(p);
        if(*p=='}') return NULL;
        if(*p++!=',') return NULL;
    }
    return NULL;
}
static int sample_json_text(const char *value,const char *expected)
{
    if(!value) return 0;
    const size_t length=strlen(expected);
    const char *end=sample_json_skip(value,0);
    return end && *value=='"' && (size_t)(end-value)==length+2 && !memcmp(value+1,expected,length);
}
#endif
