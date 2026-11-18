#ifndef FORM_FLOW_CORE_H
#define FORM_FLOW_CORE_H
#include <stdint.h>
/* Event-loop-owned values. No UI, transport, authentication or persistence. */
#define APPLICATION_FORM_FIELDS 14u
#define APPLICATION_FORM_CAPACITY 8193u
typedef struct { int step;char body[APPLICATION_FORM_CAPACITY]; } ApplicationForm;
typedef enum { APPLICATION_VALUE,APPLICATION_CONFIRM,APPLICATION_RESTART,APPLICATION_CANCEL } ApplicationInput;
typedef enum { APPLICATION_INACTIVE,APPLICATION_PROMPT,APPLICATION_INVALID,
               APPLICATION_REVIEW,APPLICATION_CONFIRM_PROMPT,APPLICATION_PASSWORD,
               APPLICATION_ABORTED } ApplicationEffect;
void application_form_begin(ApplicationForm *);
/* Securely erase staged personal information, including bytes after the terminator. */
void application_form_clear(ApplicationForm *);
/* Borrowed static key for the current input, NULL at review/inactive. */
const char *application_form_field(const ApplicationForm *);
/* profile_valid comes from the existing Profile validator. Inputs are terminated
 * and may not alias body. Confirm/restart are actions only at the review step;
 * during field input the original text remains a value. */
ApplicationEffect application_form_input(ApplicationForm *,ApplicationInput,const char *,int profile_valid);

typedef struct { uint32_t target;unsigned field; } ProfileForm;
typedef enum { PROFILE_FORM_SELF,PROFILE_FORM_MEMBER,PROFILE_FORM_SEARCH } ProfileFormKind;
typedef struct { ProfileFormKind kind;uint32_t target;const char *field; } ProfileFormRequest;
unsigned profile_form_count(int search);
const char *profile_form_field(unsigned index);
void profile_form_target(ProfileForm *,uint32_t);
/* One-based user selection. Invalid selection leaves the prior field intact. */
int profile_form_select(ProfileForm *,int search,uint32_t selection);
/* A typed intent, never an authorization. Worker still checks actor/target/ACL. */
ProfileFormRequest profile_form_request(const ProfileForm *,int search);
#endif
