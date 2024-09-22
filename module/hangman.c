// SPDX-License-Identifier: MIT
/*
 * module.c - Implements the hangman game
 */

// TODO: create 10 device files for the game

#include <linux/init.h>
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/ioctl.h>
#include <linux/string.h>

#ifndef IOCTL_RESET
#define IOCTL_RESET _IO(0x07, 1)
#endif

#define NUM_DEVICES 10
#define DEVICE_0_NAME "hangman_0"
#define DEVICE_NAME_LEN 9
#define MY_MISC_D_NAME "hangman"
#define SUCCESS 0
#define MAX_MISTAKES 6
#define TREE_SIZE 62
#define ABC 26

/* Interenal arguments of the game */
loff_t my_file_max_size = 0;
static char empty_tree[] =
"  _______\n"
"  |     |\n"
"  |      \n"
"  |       \n"
"  |       \n"
"  |\n"
"__|__\n";
static int limb_idx[] = {28, 38, 37, 39, 48, 50};
static char limb_shape[] = {'O', '|', '/', '\\', '/', '\\'};

enum status {
	A,
	B,
	C
};

/* per-instance game structure */
struct hangman_args {
	int secret_word_len;
	int secret_hist[ABC];
	int guessed_correct_hist[ABC];
	int guessed_incorrect_hist[ABC];
	int tries_made;
	enum status current_status;
	char *secret_word;
	char *guessed;
	char tree[TREE_SIZE + 1];
};

/* Notice, thier index correspond to one another
 * Meaning, the minor number at device_minor_nums[i]
 * had his args at args_arr[i]
 */
int device_minor_nums[NUM_DEVICES] = {0};
struct hangman_args args_arr[NUM_DEVICES];

/* Helper function, return the index of a minor number in the device_minor_nums
 * if does not exists, return -1
 */
int get_minor_idx(int minor)
{
	for (int i = 0; i < NUM_DEVICES; i++)
		if (minor == device_minor_nums[i])
			return i;
	return -1;
}

/* methods */
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
static void build_secret_histogram(struct hangman_args *args)
{
	if (!args || !args->secret_word)
		return;

	memset(args->secret_hist, 0, sizeof(args->secret_hist));

	for (int i = 0; i < args->secret_word_len; i++)
		args->secret_hist[args->secret_word[i] - 'a'] = 1;
}

/* Helper function, updated the tree after a wrong guess */
void update_tree_add_limb(struct hangman_args *args)
{
	if (!args || args->tries_made == 0)
		return;

	args->tree[limb_idx[args->tries_made - 1]] = limb_shape[args->tries_made - 1];
}

/* Helper function, updated the guess word w.r.t. a given char */
void update_guess_word(struct hangman_args *args, char *char_to_guess)
{
	if (!args)
		return;

	for (int i = 0; i < args->secret_word_len; i++)
		if (args->secret_word[i] == char_to_guess[0])
			args->guessed[i] = char_to_guess[0];
}

/* Helper function, checks if secret were descovered */
int check_if_secret_found(struct hangman_args *args)
{
	if (!args)
		return 0;

	for (int i = 0; i < ABC; i++)
		if (args->guessed_correct_hist[i] != args->secret_hist[i])
			return 0;
	
	return args->tries_made < MAX_MISTAKES;
}

/* Helper function, updated the hists and tries_made w.r.t. a given char
 * if the char_to_guess is corrent, and was not discoved thus far
 * return the number of appearances it has, o.w. -1
 */
void update_game_params(struct hangman_args *args, char *char_to_guess)
{
	if (!char_to_guess || !args)
		return;

	int char_idx = char_to_guess[0] - 'a';

	// correct guess
	if (args->secret_hist[char_idx] == 1) {
		if (args->guessed_correct_hist[char_idx] == 1)
			return; // was already guessed
		
		update_guess_word(args, char_to_guess);
		args->guessed_correct_hist[char_idx] = 1;

		// game won? 
		if (check_if_secret_found(args) == 1)
			args->current_status = C;
		return;
	}

	// incorrect guess - first time
	if (args->guessed_incorrect_hist[char_idx] == 0) {
		args->tries_made++;
		args->guessed_incorrect_hist[char_idx] = 1;
		update_tree_add_limb(args);
	}

	// game over?
	if (args->tries_made == MAX_MISTAKES)
		args->current_status = C;
}

/* Helper function, resets the arguments of the game */
void reset_game_params(struct hangman_args *args)
{
	if (!args)
		return;

	args->secret_word_len = 0;
	memset(args->secret_hist, 0, sizeof(args->secret_hist));
	memset(args->guessed_correct_hist, 0, sizeof(args->guessed_correct_hist));
	memset(args->guessed_incorrect_hist, 0, sizeof(args->guessed_incorrect_hist));
	args->tries_made = 0;
	args->current_status = A;

	if (args->secret_word)
		kfree(args->secret_word);

	if (args->guessed)
		kfree(args->guessed);

	args->secret_word = NULL;
	args->guessed = NULL;

	strscpy(args->tree, empty_tree, TREE_SIZE + 1);
	my_file_max_size = 0;
}

/* Called when a process tried to open the device file
 * Inserts the appropriate hangman_args in filep->private_data
 */
static int device_open(struct inode *inode, struct file *filep)
{
	int device_minor = iminor(inode);
	int device_idx = get_minor_idx(device_minor);

	if (device_idx == -1)
		return -EINVAL;

	filep->private_data = &args_arr[device_idx];

	return SUCCESS;
}

/* Called when a process closes the device file */
static int device_release(struct inode *inode, struct file *filep)
{
	return SUCCESS;
}

static ssize_t read_status_A(struct file *filep, char * __user buf, size_t count, loff_t *fpos)
{
	char *msg = "Please enter the word to be guessed\n";
	size_t msg_len = strlen(msg), bytes_not_written = 0;

	if (count == 0)
		return 0;

	if (!buf)
		return -EFAULT;

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
	struct hangman_args *args = filep->private_data;
	int total_str_len = TREE_SIZE + args->secret_word_len + 1;
	char *total_str = kmalloc(total_str_len + 1, GFP_KERNEL);

	if (!total_str)
		return -ENOMEM;

	strscpy(total_str, args->guessed, args->secret_word_len + 1);
	total_str[(unsigned int)args->secret_word_len] = '\n';
	strcat(total_str + args->secret_word_len + 1, args->tree);
	total_str[(unsigned int)args->secret_word_len + TREE_SIZE + 2] = '\0';

	if (*fpos >= total_str_len)
		goto out;

	if (count > total_str_len - *fpos)
		count = total_str_len - *fpos;

	bytes_not_written = copy_to_user(buf, total_str + *fpos, count);

	if (bytes_not_written) {
		retval = -EINVAL;
		goto out;
	}

	retval = count - bytes_not_written;
	*fpos += retval;// yea?

out:
	kfree(total_str);
	return retval;
}

static ssize_t read_status_C(struct file *filep, char * __user buf, size_t count, loff_t *fpos)
{
	return read_status_B(filep, buf, count, fpos);
}

/* called when somebody tries to read from out device file */
static ssize_t device_read(struct file *filep, char * __user buf, size_t count, loff_t *fpos)
{
	ssize_t res = 0;
	struct hangman_args* args = filep->private_data;

	if (!args)
		return -EINVAL;

	switch (args->current_status) {
	case A:
		res = read_status_A(filep, buf, count, fpos);
		break;
	case B:
		res = read_status_B(filep, buf, count, fpos);
		break;
	case C:
		res = read_status_C(filep, buf, count, fpos);
		break;
	default:
		res = -EINVAL;
		break;
	}

	return res;
}

static ssize_t device_write_A(struct file *filep, const char __user *buf,
			      size_t count, loff_t *fpos)
{
	ssize_t retval = 0;

	if (count == 0)
		goto invalid_arg_error;

	struct hangman_args *args = filep->private_data;

	args->secret_word = kmalloc(count, GFP_KERNEL);

	if (!args->secret_word)
		goto mem_error_1;

	args->guessed = kmalloc(count, GFP_KERNEL);

	if (!args->guessed)
		goto mem_error_1;

	memset(args->guessed, '*', count);

	if (copy_from_user(args->secret_word, buf, count))
		goto mem_error_2;

	if (!string_all_a_z(args->secret_word, count)) {
		pr_info("%s got string [%s] which is not all lower case a-z\n",
			__func__, args->secret_word);
		goto invalid_arg_error;
	}

	args->secret_word_len = count;
	my_file_max_size = count; //TODO: um?
	build_secret_histogram(args);
	args->tries_made = 0;
	args->current_status = B;
	retval = count;
	goto out;

invalid_arg_error:
	pr_err("%s got invalid args\n", __func__);
	retval = -EINVAL;
	goto reset;

mem_error_1:
	pr_err("%s got mem error", __func__);
	retval = -ENOMEM;
	goto reset;

mem_error_2:
	pr_err("%s got mem error", __func__);
	retval = -EFAULT;
	goto reset;

reset:
	reset_game_params(args);

out:
	return retval;
}

/* handle one char at a time */
static ssize_t device_write_one_char_B(struct file *filep, const char __user *buf,
				       size_t count, loff_t *fpos)
{
	struct hangman_args *args = filep->private_data;

	if (args->tries_made == MAX_MISTAKES || args->current_status == C)
		return -EROFS; // finished the game

	if (count == 0)
		return 0;

	char char_to_guess[2] = "";

	if (copy_from_user(char_to_guess, buf + *fpos, 1))
		return -EINVAL;

	char_to_guess[1] = '\0';

	if (!string_all_a_z(char_to_guess, 1))
		return -EINVAL;

	update_game_params(args, char_to_guess);
	*fpos += 1;
	return 1;
}

static ssize_t device_write_B(struct file *filep, const char __user *buf,
				       size_t count, loff_t *fpos)
{
	if (count == 0)
		return 0;

	struct hangman_args *args = filep->private_data;
	ssize_t bytes_written = 0;
	ssize_t write_B_retval = device_write_one_char_B(filep, buf, count, fpos);

	if (write_B_retval <= 0)
		return write_B_retval;

	bytes_written++;
	// keep trying to write one byte at a time
	while (bytes_written < count && write_B_retval > 0 && args->current_status == B) {
		write_B_retval = device_write_one_char_B(filep, buf, count - bytes_written, fpos);
		bytes_written++;
	}

	if (write_B_retval == -EROFS) //finished the game
		return bytes_written;
	else if (write_B_retval < 0) // eror
		return write_B_retval;
	return bytes_written;
}

static ssize_t device_write_C(struct file *filep, const char __user *buf,
			      size_t count, loff_t *fpos)
{
	return -EINVAL;
}

/* called when somebody tries to write into our device file */
static ssize_t device_write(struct file *filep, const char __user *buf, size_t count, loff_t *fpos)
{
	if (count == 0)
		return 0;

	*fpos = 0;
	ssize_t res = 0;

	struct hangman_args *args = filep->private_data;

	switch (args->current_status) {
	case A:
		res = device_write_A(filep, buf, count, fpos);
		// if succesfull, current_status was changed to B
		break;
	case B:
		res = device_write_B(filep, buf, count, fpos);
		// if the is game over, there won't be progess
		break;
	case C:
		res = device_write_C(filep, buf, count, fpos);
		// game over
		break;
	default:
		res = -EINVAL;
		break;
	}

	*fpos = 0; // after each complete write, we reset the fpos
	return res;
}

static long device_ioctl(struct file *filep, unsigned int cmd, unsigned long arg)
{
	switch(cmd) {
	case IOCTL_RESET:
		// reset the game
		reset_game_params(filep->private_data);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static struct file_operations device_fops = {
	.owner = THIS_MODULE,
	.open = device_open,
	.release = device_release,
	.read = device_read,
	.write = device_write,
	.llseek = generic_file_llseek,
	.unlocked_ioctl = device_ioctl,
};

static struct miscdevice my_misc_devices[NUM_DEVICES];

/*
 * Modules init function
 * Init all the device files, and thier repective hangman_args
 */
static int __init my_misc_driver_init(void)
{
	int ret = 0, i = 0;
	for (i = 0; i < NUM_DEVICES; i++) {

		char device_i_name[DEVICE_NAME_LEN + 1] = DEVICE_0_NAME;
		device_i_name[DEVICE_NAME_LEN - 1] = device_i_name[DEVICE_NAME_LEN - 1] + i;

		my_misc_devices[i].minor = MISC_DYNAMIC_MINOR;
		my_misc_devices[i].name = device_i_name;
		my_misc_devices[i].fops = &device_fops;
		my_misc_devices[i].mode = 0666;

		ret = misc_register(&my_misc_devices[i]);
		device_minor_nums[i] = my_misc_devices[i].minor;
		reset_game_params(&args_arr[i]);

		if (ret) {
			while (i--)
				misc_deregister(&my_misc_devices[i]);
			return ret;
		}
	}

	pr_info("%d hangman devices registered\n", NUM_DEVICES);
	return SUCCESS;
}

static void __exit my_misc_device_exit(void)
{
	for (int i = 0; i < NUM_DEVICES; i++) {
		misc_deregister(&my_misc_devices[i]);
		reset_game_params(&args_arr[i]);
	}

	pr_info("%d hangman devices unregistered\n", NUM_DEVICES);
}

module_init(my_misc_driver_init);
module_exit(my_misc_device_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("yakir-david");
MODULE_DESCRIPTION("A simple hangman game module");
