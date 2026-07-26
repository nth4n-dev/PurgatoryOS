/* include/kernel/exceptions.h */

#ifndef PURGATORY_EXCEPTIONS_H
#define PURGATORY_EXCEPTIONS_H

void exceptions_init(void);
void sync_exception_handler(void);
void irq_handler(void);

#endif //PURGATORY_EXCEPTIONS_H
