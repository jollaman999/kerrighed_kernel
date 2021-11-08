/*
 *  Copyright (C) 2019-2021 Innogrid HCC.
 */

#include <hcc/hccsyms.h>
#include <linux/module.h>
#include <linux/hcc_hashtable.h>
#include <linux/init.h>

/*****************************************************************************/
/*                                                                           */
/*                          HCC KSYM MANAGEMENT                        */
/*                                                                           */
/*****************************************************************************/


#define HCCSYMS_HTABLE_SIZE 256

static hashtable_t *hccsyms_htable;
static void* hccsyms_table[HCCSYMS_TABLE_SIZE];

int hccsyms_register(enum hccsyms_val v, void* p)
{
	if( (v < 0) || (v >= HCCSYMS_TABLE_SIZE) ){
		printk("hccsyms_register: Incorrect hccsym value (%d)\n", v);
		BUG();
		return -1;
	};

	if(hccsyms_table[v])
		printk("hccsyms_register_symbol(%d, %p): value already set in table\n",
					 v, p);

	if(hashtable_find(hccsyms_htable, (unsigned long)p) != NULL)
	{
		printk("hccsyms_register_symbol(%d, %p): value already set in htable\n",
					 v, p);
		BUG();
	}

	hashtable_add(hccsyms_htable, (unsigned long)p, (void*)v);
	hccsyms_table[v] = p;

	return 0;
};
EXPORT_SYMBOL(hccsyms_register);

int hccsyms_unregister(enum hccsyms_val v)
{
	void *p;

	if( (v < 0) || (v >= HCCSYMS_TABLE_SIZE) ){
		printk("hccsyms_unregister: Incorrect hccsym value (%d)\n", v);
		BUG();
		return -1;
	};

	p = hccsyms_table[v];
	hccsyms_table[v] = NULL;
	hashtable_remove(hccsyms_htable, (unsigned long)p);

	return 0;
};
EXPORT_SYMBOL(hccsyms_unregister);

enum hccsyms_val hccsyms_export(void* p)
{
	return (enum hccsyms_val)hashtable_find(hccsyms_htable, (unsigned long)p);
};

void* hccsyms_import(enum hccsyms_val v)
{
	if( (v < 0) || (v >= HCCSYMS_TABLE_SIZE) ){
		printk("hccsyms_import: Incorrect hccsym value (%d)\n", v);
		BUG();
		return NULL;
	};

	if ((v!=0) && (hccsyms_table[v] == NULL))
	{
		printk ("undefined hccsymbol (%d)\n", v);
		BUG();
	}

	return hccsyms_table[v];
};

int __init init_hccsyms(void)
{
	hccsyms_htable = hashtable_new(HCCSYMS_HTABLE_SIZE);
	if (!hccsyms_htable)
		panic("Could not setup hccsyms table!\n");

	return 0;
};
