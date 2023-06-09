#include <linux/buffer_head.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/time.h>
#include <linux/timekeeping.h>
#include <linux/types.h>

#include "singlefilefs.h"

//this iterate function just returns 3 entries: . and .. and then the name of the unique file of the file system
//è una funzione invocata ogni volta che viene richiesto il contenuto di una directory (vedi il comando ls).

/*@param dir_context* ctx: struttura che mantiene lo stato dell'iterazione della directory.
 * Il suo campo pos indica la posizione attuale nella directory.
 */
static int onefilefs_iterate(struct file *file, struct dir_context* ctx) {

//	printk("%s: we are inside readdir with ctx->pos set to %lld", MOD_NAME, ctx->pos);
	
	if(ctx->pos >= (2 + 1)) return 0;//we cannot return more than . and .. and the unique file entry

	if (ctx->pos == 0){
//   	printk("%s: we are inside readdir with ctx->pos set to %lld", MOD_NAME, ctx->pos);
		if(!dir_emit(ctx,".", 1, SINGLEFILEFS_ROOT_INODE_NUMBER, DT_UNKNOWN)){
			return 0;
		}
		else{
			ctx->pos++;
		}
	
	}

	if (ctx->pos == 1){
//  	printk("%s: we are inside readdir with ctx->pos set to %lld", MOD_NAME, ctx->pos);
		//here the inode number does not care
		if(!dir_emit(ctx,"..", 2, 1, DT_UNKNOWN)){
			return 0;
		}
		else{
			ctx->pos++;
		}
	
	}
	if (ctx->pos == 2){
// 		printk("%s: we are inside readdir with ctx->pos set to %lld", MOD_NAME, ctx->pos);
		if(!dir_emit(ctx, UNIQUE_FILE_NAME, strlen(UNIQUE_FILE_NAME), SINGLEFILEFS_FILE_INODE_NUMBER, DT_UNKNOWN)){
			return 0;
		}
		else{
			ctx->pos++;
		}
	
	}

	return 0;

}

//add the iterate function in the dir operations
const struct file_operations onefilefs_dir_operations = {
    .owner = THIS_MODULE,
    .iterate = onefilefs_iterate,
};
