#ifndef NUTSHELL_NS_REDUCED_MOTION_H
#define NUTSHELL_NS_REDUCED_MOTION_H

/* Accessor for window.c's live "reduced motion" flag (Design-System
 * Foundation, task 8): read once at startup and again on every
 * WM_SETTINGCHANGE via SystemParametersInfo(SPI_GETCLIENTAREAANIMATION),
 * so any src/ui translation unit driving its own animation can snap
 * straight to the end state when the user has turned system animations
 * off. Non-zero means "reduce/skip motion". */

int ns_reduced_motion(void);

#endif /* NUTSHELL_NS_REDUCED_MOTION_H */
