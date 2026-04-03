#include "htaccess_exec_forcetype.h"
#include <string.h>

int exec_force_type(lsi_session_t *session, const htaccess_directive_t *dir)
{
    if (!session || !dir || !dir->value)
        return LSI_OK;

    lsi_session_set_resp_content_type(session,
                                       dir->value, (int)strlen(dir->value));
    return LSI_OK;
}
