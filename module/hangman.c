// SPDX-License-Identifier: MIT
/*
 * module.c - Implements the hangman game
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/uaccess.h>

#define MY_MISC_D_NAME "hangman"
#define SUCCESS 0
#define MAX_MISTAKES 6
#define TREE_SIZE 62
#define ABC 26

enum status {
	A,
	B,
	C
};

/* Interenal arguments of the game */
static enum status current_status = A;
static char *secret_word;
static char *guessed;
static char tree[] =
"  _______\n"
"  |     |\n"
"  |      \n"
"  |       \n"
"  |       \n"
"  |\n"
"__|__\n";
static char secret_word_len;
static int secret_hist[ABC] = {0};
static int guessed_correct_hist[ABC] = {0};
static int guessed_incorrect_hist[ABC] = {0};
static int tries_made = 0;
static int limb_idx[] = {28, 38, 37, 39, 48, 50};
static char limb_shape[] = {'O', '|', '/', '\\', '/', '\\'};

/* methods */

/* Called when a process closes the device file */
static int device_open(struct inode *inode, struct file *filep)
{
	return SUCCESS;
}

/* Called when a process tried to open the device file */
static int device_release(struct inode *inode, struct file *file)
{
	return SUCCESS;
}

/* helper function, checks if a given string is constructed by lower letters a-z only */
static int string_all_a_z(char *str, int len)
{
	if (!str)
		return 0;

	for (int i = 0; i < len; i++)
		if (str[i] < 'a' || str[i] > 'z')
			return 0;
	return 1;
}

/* Helper function, builds the secret histogram of the secret word */
static void build_secret_histogram(void)
{
	if (!secret_word)
		return;

	for (int i = 0; i < secret_word_len; i++)
		secret_hist[secret_word[i] - 'a'] = 1;
}

/* Helper function, updated the tree after a wrong guess */
void update_tree_add_limb(void)
{
	if (tries_made == 0)
		return;
	tree[limb_idx[tries_made - 1]] = limb_shape[tries_made - 1];
}

/* Helper function, updated the guess word w.r.t. a given char */
void update_guess_word(char *char_to_guess)
{
	for (int i = 0; i < secret_word_len; i++)
		if (secret_word[i] == char_to_guess[0])
			guessed[i] = char_to_guess[0];
}

/* Helper function, updated the hists and tries_made w.r.t. a given char */
void update_game_params(char *char_to_guess)
{
	if (!char_to_guess)
		return;

	int char_idx = char_to_guess[0] - 'a';

	// correct guess
	if (secret_hist[char_idx] == 1) {
		guessed_correct_hist[char_idx] = 1;
		update_guess_word(char_to_guess);
		return;
	}

	// incorrect guess
	if (guessed_incorrect_hist[char_idx] == 0) {
		tries_made++;
		guessed_incorrect_hist[char_idx] = 1;
		update_tree_add_limb();
	}

	// game over? TODO: complete
}

static ssize_t read_status_A(struct file *filep, char * __user buf, size_t count, loff_t *fpos)
{
	char *msg = "Please enter the word to be guessed\n";
	size_t msg_len = strlen(msg), bytes_not_written = 0;

	if (*fpos >= msg_len)
		return 0;

	if (count > msg_len - *fpos)
		count = msg_len - *fpos;

	bytes_not_written = copy_to_user(buf, msg + *fpos, count);

	if (bytes_not_written > 0)
		return -EFAULT;

	*fpos += count;

	return msg_len - bytes_not_written;
}

static ssize_t read_status_B(struct file *filep, char * __user buf, size_t count, loff_t *fpos)
{
	ssize_t retval = 0, bytes_not_written = 0;
	int total_str_len = TREE_SIZE + secret_word_len + 2;

	char *total_str = kmalloc(total_str_len + 1, GFP_KERNEL);

	if (!total_str)
		return -ENOMEM;

	strscpy(total_str, guessed, secret_word_len + 1);
	total_str[(unsigned int)secret_word_len] = '\n';
	strcat(total_str + secret_word_len + 1, tree);
	total_str[(unsigned int)secret_word_len + TREE_SIZE + 1] = '\n';
	total_str[(unsigned int)secret_word_len + TREE_SIZE + 2] = '\0';

	if (*fpos >= total_str_len)
		goto out;

	if (count > total_str_len - *fpos)
		count = total_str_len - *fpos;

	bytes_not_written = copy_to_user(buf, total_str + *fpos, count);

	if (bytes_not_written) {
		retval = -EFAULT;
		goto out;
	}

	retval = count - bytes_not_written;
	*fpos += retval;

out:
	kfree(total_str);
	return retval;
}

/* called when somebody tries to read from out device file */
static ssize_t device_read(struct file *filep, char * __user buf, size_t count, loff_t *fpos)
{
	// TODO: check if count <= 0
	ssize_t res = 0;

	switch (current_status) {
	case A:
		res = read_status_A(filep, buf, count, fpos);
		break;
	case B:
		res = read_status_B(filep, buf, count, fpos);
		break;
	case C:
		res = -EFAULT;
		break;
	default:
		res = -EFAULT;
		break;
	}

	return res;
}

static ssize_t device_write_A(struct file *filep, const char __user *buf,
			      size_t count, loff_t *fpos)
{
	ssize_t retval = -EFAULT;

	secret_word = kmalloc(count + 1, GFP_KERNEL);

	if (!secret_word)
		return -ENOMEM;

	guessed = kmalloc(count + 1, GFP_KERNEL);

	if (!guessed)
		goto mem_error_2;

	memset(guessed, '*', count);

	if (copy_from_user(secret_word, buf, count))
		goto mem_error_1;

	if (!string_all_a_z(secret_word, count)) {
		pr_info("%s got string [%s] which is not all lower case a-z\n",
			__func__, secret_word);
		goto mem_error_1;
	}

	secret_word_len = count;
	build_secret_histogram();
	tries_made = 0;
	current_status = B;
	*fpos = 0;
	retval = count;
	goto out;

mem_error_1:
	pr_info("%s got mem error", __func__);
	retval = -EFAULT;
	kfree(guessed);
	guessed = NULL;

mem_error_2:
	kfree(secret_word);
	secret_word = NULL;

out:
	current_status = B; /* This is an important change for the course of the game */
	return retval;
}

/* handle one char at a time */
static ssize_t device_write_B(struct file *filep, const char __user *buf,
			      size_t count, loff_t *fpos)
{
	if (tries_made == MAX_MISTAKES) {
		pr_info("Max mistakes were made, game over. iocl 1 to play again.\n");
		return -EFAULT;
	}

	ssize_t retval = -EFAULT;
	char *char_to_guess = kmalloc(2, GFP_KERNEL);

	if (copy_from_user(char_to_guess, buf, 1))
		goto mem_error_2;

	char_to_guess[1] = '\0';

	if (!string_all_a_z(char_to_guess, 1)) {
		pr_info("%s got char [%s] which is not all lower case a-z\n",
			__func__, char_to_guess);
		goto mem_error_1;
	}

	update_game_params(char_to_guess);
	*fpos = 0;
	retval = 1;
	goto out;

mem_error_1:
	kfree(char_to_guess);
	char_to_guess = NULL;

mem_error_2:
	pr_info("%s for mem error", __func__);
	return -EFAULT;

out:
	kfree(char_to_guess);
	char_to_guess = NULL;
	return retval;
}

/* called when somebody tries to write into our device file */
static ssize_t device_write(struct file *filep, const char __user *buf, size_t count, loff_t *fpos)
{
	// TODO: check if count <= 0
	ssize_t res = 0;

	switch (current_status) {
	case A:
		res = device_write_A(filep, buf, count, fpos);
		// if succesfull, current_status was changed to B
		break;
	case B:
		res = device_write_B(filep, buf, count, fpos);
		// if game over, there won't be progess
		break;
	case C:
		res = -EFAULT;
		break;
	default:
		res = -EFAULT;
		break;
	}

	return res;
}

static struct file_operations device_fops = {
	.owner = THIS_MODULE,
	.open = device_open,
	.release = device_release,
	.read = device_read,
	.write = device_write,
};

static struct miscdevice my_misc_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = MY_MISC_D_NAME,
	.fops = &device_fops,
	.mode = 0666,
};

static int __init my_misc_driver_init(void)
{
	int ret = misc_register(&my_misc_device);

	if (ret) {
		pr_err("Unable to register misc device\n");
		return ret;
	}

	pr_info("Misc device registered: /dev/%s\n", my_misc_device.name);
	return SUCCESS;
}

static void __exit my_misc_device_exit(void)
{
	/* free dynammically allocated data */
	kfree(secret_word);
	kfree(guessed);
	misc_deregister(&my_misc_device);
	pr_info("Misc device unregistered: /dev/%s\n", my_misc_device.name);
}

module_init(my_misc_driver_init);
module_exit(my_misc_device_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("yakir-david");
MODULE_DESCRIPTION("A simple hangman game module");
