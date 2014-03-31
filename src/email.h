#ifndef EMAIL_H
#define EMAIL_H

#ifndef EMAIL_SERVER_ADDR
#define EMAIL_SERVER_ADDR "smtp.tcnj.edu"
#endif

#ifndef EMAIL_SERVER_PORT
#define EMAIL_SERVER_PORT 25
#endif

#ifndef EMAIL_ADDR_ADMIN
#define EMAIL_ADDR_ADMIN "admin@tcnj.edu"
#endif

int email_send(const char *address, const char *body);

#endif
