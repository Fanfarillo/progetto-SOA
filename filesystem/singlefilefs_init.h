#ifndef _ONEFILEFSINIT_H
#define _ONEFILEFSINIT_H

//this is the default content of the unique file
char *file_body[] = {
    "Abbiamo lezione solo a sogene non mi va di spostarmi avanti e indietro anche se non mi piace andare a sogene\n",
    "Forse non ci siamo capiti nell'audio di ieri\n",
    "Intanto solo per te un saluto dal mitico\n",
    "però ecco... io sono l'opposto ma solo perché sono pazzo io. What's app è la cosa minore, alla fine non mi danno fastidio i messaggi sfusi (se sono meno di 6)\n",
    "Come mi sono persa questa cosaaaaaaaaa\n"
};

//this is the superblock variable accessible by syscalls
extern struct super_block *global_sb;

#endif
