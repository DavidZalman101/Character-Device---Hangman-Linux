// SPDX-License-Identifier: MIT
/*
 * module.c - Implements the hangman game
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/ioctl.h>
#include <linux/string.h>
#include <linux/mutex.h>

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
static char empty_tree[] =
	"  _______\n" //0-9
	"  |     |\n" //10-19
	"  |      \n" //20-29
	"  |       \n" //30-40
	"  |       \n" //41-51
	"  |\n" //52-55
	"__|__\n"; //56-61
static int limb_idx[] = {28, 38, 37, 39, 48, 50};
static char limb_shape[] = {'O', '|', '/', '\\', '/', '\\'};

enum status {
	A,
	B,
	C
};

/* per-device game structure */
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
	/* protect the hangman_args data
	 * using this lock, it is up to you
	 * to always aquire the lock before
	 * looking or changing the strcut
	 */
	struct mutex arg_mutex_lock;
};

/* i-th element for the i-th device */
int device_minor_nums[NUM_DEVICES] = {0};
struct hangman_args args_arr[NUM_DEVICES];

/* Helper methods */

/* return the index of a minor number in the device_minor_nums */
int get_minor_idx(int minor)
{
	for (int i = 0; i < NUM_DEVICES; i++)
		if (minor == device_minor_nums[i])
			return i;
	return -1;
}

/* checks string is lower letters a-z only */
static int string_all_a_z(char *str, int len)
{
	if (!str)
		return 0;

	for (int i = 0; i < len; i++)
		if (str[i] < 'a' || str[i] > 'z')
			return 0;
	return 1;
}

/* builds secret hist for secret word */
static void build_secret_histogram(struct hangman_args *args)
{
	if (!args || !args->secret_word)
		return;

	memset(args->secret_hist, 0, sizeof(args->secret_hist));

	for (int i = 0; i < args->secret_word_len; i++)
		args->secret_hist[args->secret_word[i] - 'a'] = 1;
}

/* updated tree after wrong guess */
void update_tree_add_limb(struct hangman_args *args)
{
	if (!args || args->tries_made == 0)
		return;

	args->tree[limb_idx[args->tries_made - 1]] = limb_shape[args->tries_made - 1];
}

/* updated guess word w.r.t. a given char */
void update_guess_word(struct hangman_args *args, char *char_to_guess)
{
	if (!args)
		return;

	for (int i = 0; i < args->secret_word_len; i++)
		if (args->secret_word[i] == char_to_guess[0])
			args->guessed[i] = char_to_guess[0];
}

/* checks if secret was discovered */
int check_if_secret_found(struct hangman_args *args)
{
	if (!args)
		return 0;

	for (int i = 0; i < ABC; i++)
		if (args->guessed_correct_hist[i] != args->secret_hist[i])
			return 0;

	return args->tries_made < MAX_MISTAKES;
}

/* updated hists and tries_made w.r.t. a given char */
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

/* resets arguments of the game */
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
	kfree(args->secret_word);
	kfree(args->guessed);
	args->secret_word = NULL;
	args->guessed = NULL;
	strscpy(args->tree, empty_tree, TREE_SIZE + 1);
}

/* Module syscalls */

/*
 * Called when a process tries to open a device file.
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

/*
 * Called when a process closes the device file.
 * Does not release the appropriate hangman_args.
 */
static int device_release(struct inode *inode, struct file *filep)
{
	return SUCCESS;
}

/*
 * Called when a process reads the device on status A.
 * Will return the status A msg
 */
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

/*
 * Called when a process reads the device on status B.
 */
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
	*fpos += retval;

out:
	kfree(total_str);
	return retval;
}

/* Called when a process reads the device on status C */
static ssize_t read_status_C(struct file *filep, char * __user buf, size_t count, loff_t *fpos)
{
	return read_status_B(filep, buf, count, fpos);
}

/* read syscall */
static ssize_t device_read(struct file *filep, char * __user buf, size_t count, loff_t *fpos)
{
	ssize_t res = 0;
	struct hangman_args *args = filep->private_data;

	if (!args)
		return -EINVAL;

	if (mutex_lock_interruptible(&args->arg_mutex_lock))
		return -EINTR;

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

	mutex_unlock(&args->arg_mutex_lock);
	return res;
}

/* Called when a process writes to a device on status A */
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

/* helper for device_write_B, will write one char to device on status B */
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

/* Called when a process writes to device on status B */
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
	else if (write_B_retval < 0) // error
		return write_B_retval;
	return bytes_written;
}

/* Called when a process writes to device on status C */
static ssize_t device_write_C(struct file *filep, const char __user *buf,
			      size_t count, loff_t *fpos)
{
	return -EINVAL;
}

/* write syscall */
static ssize_t device_write(struct file *filep, const char __user *buf, size_t count, loff_t *fpos)
{
	ssize_t res = 0;
	struct hangman_args *args = filep->private_data;

	if (mutex_lock_interruptible(&args->arg_mutex_lock))
		return -EINTR;

	*fpos = 0; // at the beginig of each write we reset the fpos
	if (count == 0) {
		mutex_unlock(&args->arg_mutex_lock);
		return 0;
	}

	switch (args->current_status) {
	case A:
		res = device_write_A(filep, buf, count, fpos);
		// if succesfull, current_status was changed to B
		break;
	case B:
		res = device_write_B(filep, buf, count, fpos);
		// if the is game over, there won't be progress
		break;
	case C:
		res = device_write_C(filep, buf, count, fpos);
		// game over
		break;
	default:
		res = -EINVAL;
		break;
	}

	*fpos = 0; // after each complete write we reset the fpos
	mutex_unlock(&args->arg_mutex_lock);
	return res;
}

/* iocrl syscall */
static long device_ioctl(struct file *filep, unsigned int cmd, unsigned long arg)
{
	struct hangman_args *args = filep->private_data;

	if (mutex_lock_interruptible(&args->arg_mutex_lock))
		return -EINTR;

	switch (cmd) {
	case IOCTL_RESET:
		// reset the game
		reset_game_params(filep->private_data);
		break;
	default:
		mutex_unlock(&args->arg_mutex_lock);
		return -EINVAL;
	}

	mutex_unlock(&args->arg_mutex_lock);
	return 0;
}

/* llseek syscall */
static loff_t device_llseek(struct file *filep, loff_t offset, int whence)
{
	loff_t new_pos = 0;
	struct hangman_args *args = filep->private_data;

	if (mutex_lock_interruptible(&args->arg_mutex_lock))
		return -EINTR;

	switch (whence) {
	case SEEK_SET:
		// set position to offset
		if (offset < 0 || offset > args->secret_word_len)
			goto error;

		new_pos = offset;
		break;

	case SEEK_CUR:
		// update position relative to current position
		if ((filep->f_pos + offset) < 0 || (filep->f_pos + offset) > args->secret_word_len)
			goto error;

		new_pos = filep->f_pos + offset;
		break;

	case SEEK_END:
		// set position relative to the end
		if ((args->secret_word_len + offset) < 0 || offset > 0)
			goto error;

		new_pos = args->secret_word_len + offset;
		break;

	default:
		goto error;
	}

	filep->f_pos = new_pos;
	mutex_unlock(&args->arg_mutex_lock);
	return new_pos;

error:
	mutex_unlock(&args->arg_mutex_lock);
	return -EINVAL;
}

/* file operations */
static struct file_operations device_fops = {
	.owner = THIS_MODULE,
	.open = device_open,
	.release = device_release,
	.read = device_read,
	.write = device_write,
	.llseek = device_llseek,
	.unlocked_ioctl = device_ioctl,
};

/* misc devices */
static struct miscdevice my_misc_devices[NUM_DEVICES];

/* Modules init function */
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
		mutex_init(&args_arr[i].arg_mutex_lock);

		// deregister the first i-1 devices
		if (ret) {
			while (i--)
				misc_deregister(&my_misc_devices[i]);
			return ret;
		}
	}

	pr_info("%d hangman devices registered\n", NUM_DEVICES);
	return SUCCESS;
}

/* destroy all devices and dynammic allocated memory */
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
