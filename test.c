// SPDX-License-Identifier: MIT
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <string.h>
#include <sys/ioctl.h>
#include <pthread.h>
#include "hangman_device_ioctl.h"

#define NUM_TESTS 25
#define NON_ZERO 1
#define READ_BUFFER_SIZE 500 // hangman drawing+secret_word will be less
//#define IOCTL_RESET 1
#define HANGMAN_SIZE
const char *read_in_a = "Please enter the word to be guessed\n";
const char *filename = "/dev/hangman";
const char *secret_word = "apple";
const char *secret_word_hidden = "*****";
// testing character should be one of the secret word characters
const char *testing_char = "a";
#define SECRET_WORD_LEN 5
#define SECRET_WORD_LEN_WITH_NEWLINE 6
int current_func_num = -1;

#define PRINT_ERR(fmt, ...) \
	printf("not ok %d - " fmt "\n", current_func_num, ##__VA_ARGS__)
#define PRINT_OK(...)                                               \
	printf("ok %d - %s - Passed\n", current_func_num, __func__, \
	       ##__VA_ARGS__)

#define HOOK_AND_INVOKE(iterator, func) func[iterator]()
const char *hangman_full = //the formatted stick-man below
	"  _______\n" //0-9
	"  |     |\n" //10-19
	"  |     O\n" //20-29
	"  |    /|\\\n" //30-40
	"  |    / \\\n" //41-51
	"  |\n" // 52-55
	"__|__\n"; // 56-61
const char *hangman_empty = //the empty stick-man below
	"  _______\n" //0-9
	"  |     |\n" //10-19
	"  |      \n" //20-29
	"  |       \n" //30-40
	"  |       \n" //41-51
	"  |\n" //52-55
	"__|__\n"; //56-61
int hang_man_char_index[6] = { 28, 37, 38, 39, 48, 50 };
#define HANGMAN_DRAWING_SIZE 62
#define HANGMAN_AND_SECRET_WORD_SIZE \
	(HANGMAN_DRAWING_SIZE + SECRET_WORD_LEN_WITH_NEWLINE)

// helpers funcs
ssize_t read_helper(int file, void *buffer, size_t bytes)
{
	bool read_EINTR = false;
	ssize_t bytes_read;

	while (!read_EINTR) {
		bytes_read = read(file, buffer, bytes);
		if (!(bytes_read == -1 && errno == EINTR)) {
			read_EINTR = true;
		}
	}
	return bytes_read;
}

int is_all_a_z(const char *str)
{
	while (*str) { // Loop through each character in the string
		if (*str < 'a' || *str > 'z') {
			PRINT_ERR("expected all secret word characters to be in a-z");
			return 0; // If any character is not in a-z, return 0 (false)
		}
		str++;
	}
	return 1; // All characters are in a-z, return 1 (true)
}

int check_if_in_A(int fd)
{
	int is_success = 1;
	int llseek_ret = lseek(fd, 0, SEEK_SET);

	if (llseek_ret == -1) {
		PRINT_ERR("%s; unexpected error while lseek errno=%d", __func__, errno);
		return -1;
	}
	char buffer[READ_BUFFER_SIZE];
	ssize_t bytes_read = read_helper(fd, buffer, READ_BUFFER_SIZE);

	if (bytes_read == -1) {
		PRINT_ERR("%s; unexpected error while reading errno=%d", __func__, errno);
		return -1;
	}
	buffer[bytes_read] = '\0';

	if (strcmp(buffer, read_in_a) != 0) {
		is_success = 0;
	}
	llseek_ret = lseek(fd, 0, SEEK_SET);
	if (llseek_ret == -1) {
		PRINT_ERR("%s; unexpected error while lseek errno=%d", __func__, errno);
		return -1;
	}
	return is_success;
}

int check_if_in_C(int fd, const char *some_char)
{
	/* we are in C state, we should get errno EINVAL in case
	 * we try to write a legit character
	 */

	if (!is_all_a_z(some_char)) {
		PRINT_ERR("%s; not all chars are a-z", __func__);
		return -1;
	}

	ssize_t bytes_write = write(fd, some_char, 1);

	if (bytes_write == -1 && errno == EINVAL)
		return 1;
	return 0;
}

int pick_secret_word(int fd, const char *secret_word, int secret_word_len);

int do_reset(int fd);

int validate_if_in_A(int fd);

int run_full_game_and_reset(int file, const char *secret, int secret_len, int expected_write_ret)
{
	bool is_success = true;

	// we are in A state, lets write a secret word
	int write_secret_word_ret = pick_secret_word(file, secret, secret_len);

	if (!write_secret_word_ret) {
		is_success = false;
		goto close;
	}
	// now we are in B state, lets write a correct guess characters
	int write_correct_guess_ret = write(file, secret, secret_len);

	if (write_correct_guess_ret != expected_write_ret) { // test was wrong, it will finish earlier!
		PRINT_ERR("unexpected error while writing, errno=%d", errno);
		is_success = false;
		goto close;
	}
	// now we are in B state, lets read
	char *buffer = malloc(secret_len + 1 + HANGMAN_DRAWING_SIZE);
	ssize_t bytes_read = read_helper(file, buffer, READ_BUFFER_SIZE);

	if (bytes_read != HANGMAN_DRAWING_SIZE + secret_len + 1) {
		PRINT_ERR("unexpected error while reading, errno=%d", errno);
		is_success = false;
		goto close;
	}
	char *buffer_tmp  = malloc(HANGMAN_DRAWING_SIZE + 1 + secret_len);

	strcpy(buffer_tmp, secret);
	buffer_tmp[secret_len] = '\n';
	strcat(buffer_tmp + secret_len + 1, hangman_empty);

	if (strncmp(buffer, buffer_tmp, secret_len + 1 + HANGMAN_DRAWING_SIZE) != 0) {
		PRINT_ERR("read secret word and hangman drawing are not as expected");
		is_success = false;
		goto close;
	}
	// we are in C , lets reset
	int do_reset_ret = do_reset(file);

	if (do_reset_ret != 1) {
		is_success = false;
		goto close;
	}
	// now we should be in A states, lets check.
	if (!validate_if_in_A(file)) {
		is_success = false;
		goto close;
	}

close:
	free(buffer);
	free(buffer_tmp);
	return is_success;

	// wtf is this ameture bs? 
	if (is_success) {
		return 1;
	} else {
		return 0;
	}
}

int check_if_in_B(int fd)
{
	// we are in B state, we should not be in A or C state
	int check_if_in_A_ret = check_if_in_A(fd);

	if (check_if_in_A_ret == -1) {
		return -1;
	}
	int check_if_in_C_ret = check_if_in_C(fd, testing_char);

	if (check_if_in_C_ret == -1) {
		return -1;
	}
	if (check_if_in_A_ret == 1 || check_if_in_C_ret == 1) {
		return 0;
	}
	return 1;
}

int validate_if_in_A(int fd)
{
	int check_if_in_A_ret = check_if_in_A(fd);

	if (check_if_in_A_ret == 0) {
		PRINT_ERR("should read \"Please enter the word to be guessed\n\"");
		return 0;
	} else if (check_if_in_A_ret == -1) {
		return 0;
	}
	return 1;
}

int validate_if_in_C(int fd, const char *some_char)
{
	int check_if_in_C_ret = check_if_in_C(fd, some_char);

	if (check_if_in_C_ret == 0) {
		PRINT_ERR("expected to be in C state but we are not, errno=%d", errno);
		return 0;
	} else if (check_if_in_C_ret == -1) {
		return 0;
	}
	return 1;
}

int validate_if_in_B(int fd)
{
	int check_if_in_B_ret = check_if_in_B(fd);

	if (check_if_in_B_ret == 0) {
		PRINT_ERR("expected to be in B state but we are not, errno=%d", errno);
		return 0;
	} else if (check_if_in_B_ret == -1) {
		return 0;
	}
	return 1;
}

int pick_secret_word(int fd, const char *secret_word, int secret_word_len)
{
	ssize_t bytes_write = write(fd, secret_word, secret_word_len);

	if (bytes_write != secret_word_len) {
		PRINT_ERR("unexpected error while writing, errno=%d", errno);
		return 0;
	}
	return 1;
}

int do_reset(int fd)
{
	int ioctl_ret = ioctl(fd, IOCTL_RESET, NULL);

	if (ioctl_ret != 0) {
		PRINT_ERR("unexpected error invoking ioctl; errno=%d", errno);
		return 0;
	}
	return 1;
}

int file_exists(void)
{
	struct stat buffer;

	if (stat(filename, &buffer) != 0) {
		PRINT_ERR("File %s does not exist", filename);
		return 0;
	}
	return 1;
}

int check_file_permissions_helper(void)
{
	struct stat buffer;

	stat(filename, &buffer);
	int is_owner_read_only = (buffer.st_mode & 0600);
	int is_group_read_only = (buffer.st_mode & 0060);
	int is_others_read_only = (buffer.st_mode & 0006);

	if (!(is_owner_read_only && is_group_read_only &&
	      is_others_read_only)) {
		PRINT_ERR("File %s is not read-write by owner/usr/group", filename);
		return 0;
	}
	return 1;
}

int open_file(void)
{
	if (!file_exists())
		return 0;
	if (!check_file_permissions_helper())
		return 0;
	int file = open(filename, 2);
	ioctl(file, IOCTL_RESET, NULL);

	if (file == -1) {
		PRINT_ERR("File Couldn't Open");
		return 0;
	}
	if (!validate_if_in_A(file)) {
		return 0;
	}
	return file;
}

// general
void check_null_dereference(void)
{
	bool is_success = true;

	int file = open_file();

	if (!file)
		return;

	char *buffer = NULL;
	ssize_t bytes_read = read_helper(file, buffer, NON_ZERO);

	if (bytes_read == -1 && errno != EFAULT) {
		PRINT_ERR("expected EFAULT but got errno=%d", errno);
		is_success = false;
		goto close;
	}
	ssize_t bytes_write = write(file, buffer, NON_ZERO);

	if (!(bytes_write == -1 && errno == EFAULT)) {
		PRINT_ERR("expected EFAULT but got errno=%d", errno);
		is_success = false;
		goto close;
	}

close:
	close(file);
	if (is_success)
		PRINT_OK();
}

void check_invaild_buffer_address(void)
{
	bool is_success = true;
	int file = open_file();

	if (!file)
		return;

	char *buffer = (void *)-1;
	ssize_t bytes_read = read_helper(file, buffer, NON_ZERO);

	if (!(bytes_read == -1 && errno == EFAULT)) {
		PRINT_ERR("expected EFAULT but got errno=%d", errno);
		is_success = false;
		goto close;
	}
	ssize_t bytes_write = write(file, buffer, NON_ZERO);

	if (!(bytes_write == -1 && errno == EFAULT)) {
		PRINT_ERR("expected EFAULT but got errno=%d", errno);
		is_success = false;
		goto close;
	}
close:
	close(file);
	if (is_success)
		PRINT_OK();
}

void check_file_permissions(void)
{
	bool is_success = true;
	int file = open_file();

	if (!file)
		return;
	if (!check_file_permissions_helper()) {
		is_success = false;
		goto close;
	}
close:
	close(file);
	if (is_success)
		PRINT_OK();
}

// ioctls

/*
 * bad ioctl op num does not change the state of the
 * program and returns EINVAL.
 */
void check_invaild_ioctl(void)
{
	bool is_success = true;
	int file = open_file();

	if (!file)
		return;

	int ioctl_ret = ioctl(file, IOCTL_GARBAGE, NULL);

	if (!(ioctl_ret == -1 && errno == EINVAL)) {
		PRINT_ERR("expected EINVAL but got errno=%d", errno);
		is_success = false;
		goto close;
	}
close:
	close(file);
	if (is_success)
		PRINT_OK();
}

//In the beginning of a valid run, reset signal succeeds
void check_reset_in_A(void)
{
	bool is_success = true;
	int file = open_file();

	if (!file)
		return;
	// we are in A state, lets apply reset
	int do_reset_ret = do_reset(file);

	if (do_reset_ret != 1) {
		is_success = false;
		goto close;
	}
	// now we should be in A states, lets check.
	if (!validate_if_in_A(file)) {
		is_success = false;
		goto close;
	}
close:
	close(file);
	if (is_success)
		PRINT_OK();
}

//In the middle of a valid run, reset signal succeeds
void check_reset_in_B(void)
{
	bool is_success = true;
	int file = open_file();

	if (!file)
		return;
	// we are in A state, lets write a secret word
	int write_secret_word_ret =
		pick_secret_word(file, secret_word, SECRET_WORD_LEN);

	if (!write_secret_word_ret) {
		is_success = false;
		goto close;
	}
	// now we should be in B states, lets check.
	int valdiate_B = validate_if_in_B(file);

	if (valdiate_B != 1) {
		is_success = false;
		goto close;
	}

	int do_reset_ret = do_reset(file);

	if (do_reset_ret != 1) {
		is_success = false;
		goto close;
	}
	// now we should be in A states, lets check.
	if (!validate_if_in_A(file)) {
		is_success = false;
		goto close;
	}
close:
	close(file);
	if (is_success)
		PRINT_OK();
}

//In the end of a valid run, reset signal succeeds
void check_reset_in_C(void)
{
	bool is_success = true;
	int file = open_file();

	if (!file)
		return;
	// we are in A state, lets write a secret word
	int write_secret_word_ret =
		pick_secret_word(file, secret_word, SECRET_WORD_LEN);

	if (!write_secret_word_ret) {
		is_success = false;
		goto close;
	}
	// now we are in B state, lets write a correct guess characters
	int write_correct_guess_ret = write(file, secret_word, SECRET_WORD_LEN);

	if (write_correct_guess_ret != 5) {
		PRINT_ERR("unexpected error while writing, errno=%d", errno);
		is_success = false;
		goto close;
	}
	// we are in C states, lets validate.
	if (validate_if_in_C(file, secret_word) == 0) {
		is_success = false;
		goto close;
	}
	// we are in C , lets reset
	int do_reset_ret = do_reset(file);

	if (do_reset_ret != 1) {
		is_success = false;
		goto close;
	}
	// now we should be in A states, lets check.
	if (!validate_if_in_A(file)) {
		is_success = false;
		goto close;
	}
close:
	close(file);
	if (is_success)
		PRINT_OK();
}

/*
 * in the middle of a run that the user made errors in
 * using the syscalls, the reset also succeeds
 */
void check_bad_syscall_then_reset(void)
{
	bool is_success = true;
	int file = open_file();

	if (!file)
		return;
	// DO BAD SYSCALL(write non a-z character)
	if (write(file, "1", 1) != -1) {
		PRINT_ERR("expected to fail writing non a-z character");
		is_success = false;
		goto close;
	}
	// we are in A state, lets apply reset
	int do_reset_ret = do_reset(file);

	if (do_reset_ret != 1) {
		is_success = false;
		goto close;
	}
	// now we should be in A states, lets check.
	if (!validate_if_in_A(file)) {
		is_success = false;
		goto close;
	}
close:
	close(file);
	if (is_success)
		PRINT_OK();
}

// reads

/*
 * reading until EOF after opening returns the string
 * "Please enter the word to be guessed\n"
 */
void check_read_in_A(void)
{
	bool is_success = true;
	int file = open_file();

	if (!file) {
		return;
	}

	close(file);
	if (is_success)
		PRINT_OK();
}

/*
 * reading until EOF after deciding the word to guess
 * returns the hangman diagram without the stick figure
 * and with the current guessed word all in *
 */
void check_read_in_B(void)
{
	bool is_success = true;
	int file = open_file();

	if (!file) {
		return;
	}

	// we are in A state, lets write a secret word
	int write_secret_word_ret =
		pick_secret_word(file, secret_word, SECRET_WORD_LEN);

	if (!write_secret_word_ret) {
		is_success = false;
		goto close;
	}
	// now we are in B state, lets read
	//char buffer[READ_BUFFER_SIZE];
	char buffer[HANGMAN_AND_SECRET_WORD_SIZE + 1] = {0};
	ssize_t bytes_read = read_helper(file, buffer, READ_BUFFER_SIZE);

	if (bytes_read != HANGMAN_AND_SECRET_WORD_SIZE) {
		PRINT_ERR("unexpected error while reading, errno=%d", errno);
		is_success = false;
		goto close;
	}
	char buffer_tmp[READ_BUFFER_SIZE];

	strcpy(buffer_tmp, secret_word_hidden);
	buffer_tmp[SECRET_WORD_LEN] = '\n';
	strcat(buffer_tmp + SECRET_WORD_LEN + 1, hangman_empty);
	if (strncmp(buffer, buffer_tmp, HANGMAN_AND_SECRET_WORD_SIZE) != 0) {
		PRINT_ERR("read secret word and hangman drawing are not as expected");
		is_success = false;
		goto close;
	}

close:
	close(file);
	if (is_success)
		PRINT_OK();
}

/*
 * reading until EOF after giving one correct character
 * reveals the character without advancing the hangman stick figure
 */
void check_correct_character_not_advance_figure(void)
{
	bool is_success = true;
	int file = open_file();

	if (!file) {
		return;
	}

	// we are in A state, lets write a secret word
	int write_secret_word_ret =
		pick_secret_word(file, secret_word, SECRET_WORD_LEN);

	if (!write_secret_word_ret) {
		is_success = false;
		goto close;
	}
	// now we are in B state, lets read
	char buffer[HANGMAN_AND_SECRET_WORD_SIZE + 1] = {0};
	ssize_t bytes_read = read_helper(file, buffer, READ_BUFFER_SIZE);

	if (bytes_read != HANGMAN_AND_SECRET_WORD_SIZE) {
		PRINT_ERR("unexpected error while reading, errno=%d", errno);
		is_success = false;
		goto close;
	}
	// we are in B state, lets write a correct guess character "a"
	int write_correct_guess_ret = write(file, testing_char, 1);

	if (write_correct_guess_ret != 1) {
		PRINT_ERR("unexpected error while writing, errno=%d", errno);
		is_success = false;
		goto close;
	}

	const char *a_revealed = "a****";
	char buffer_tmp[READ_BUFFER_SIZE];
	char bf[READ_BUFFER_SIZE] = "";
	read_helper(file, bf, READ_BUFFER_SIZE);

	strcpy(buffer_tmp, a_revealed);
	buffer_tmp[SECRET_WORD_LEN] = '\n';
	strcat(buffer_tmp + SECRET_WORD_LEN + 1, hangman_empty);
	if (strncmp(bf, buffer_tmp, HANGMAN_AND_SECRET_WORD_SIZE) != 0) {
		PRINT_ERR("read secret word and hangman drawing are not as expected");
		is_success = false;
		goto close;
	}

close:
	close(file);
	if (is_success)
		PRINT_OK();
}

/*
 * reading until EOF after giving one correct character that corresponds with
 * more than one place in the word reveals all the relevant characters all at
 * once without advancing the hangman stick figure
 */
void check_multiple_occurrences_of_correct_character(void)
{
	bool is_success = true;
	int file = open_file();

	if (!file) {
		return;
	}

	// we are in A state, lets write a secret word
	int write_secret_word_ret =
		pick_secret_word(file, secret_word, SECRET_WORD_LEN);

	if (!write_secret_word_ret) {
		is_success = false;
		goto close;
	}
	// now we are in B state, lets read
	char buffer[READ_BUFFER_SIZE];
	ssize_t bytes_read = read_helper(file, buffer, READ_BUFFER_SIZE);

	if (bytes_read != HANGMAN_AND_SECRET_WORD_SIZE) {
		PRINT_ERR("unexpected error while reading, errno=%d", errno);
		is_success = false;
		goto close;
	}
	// we are in B state, lets write a correct guess character "l" that occurs twice
	int write_correct_guess_ret = write(file, "l", 1);

	if (write_correct_guess_ret != 1) {
		PRINT_ERR("unexpected error while writing, errno=%d", errno);
		is_success = false;
		goto close;
	}

	const char *l_revealed = "***l*"; // I dont even want to tell you how stupid this test was before I fixed it
	char buffer_tmp[READ_BUFFER_SIZE];
	char bf[READ_BUFFER_SIZE] = "";
	read_helper(file, bf, READ_BUFFER_SIZE);

	strcpy(buffer_tmp, l_revealed);
	buffer_tmp[SECRET_WORD_LEN] = '\n';
	strcat(buffer_tmp + SECRET_WORD_LEN + 1, hangman_empty);
	if (strcmp(bf, buffer_tmp) != 0) {
		PRINT_ERR("read secret word and hangman drawing are not as expected");
		is_success = false;
		goto close;
	}

close:
	close(file);
	if (is_success)
		PRINT_OK();
}

/*
 * reading until EOF after giving an incorrect character
 * advances the drawing of the hangman stick figure
 */
void check_incorrect_character_advance_figure(void)
{
	bool is_success = true;
	int file = open_file();

	if (!file) {
		return;
	}

	// we are in A state, lets write a secret word
	int write_secret_word_ret =
		pick_secret_word(file, secret_word, SECRET_WORD_LEN);

	if (!write_secret_word_ret) {
		is_success = false;
		goto close;
	}
	// now we are in B state, lets read
	char buffer[READ_BUFFER_SIZE];
	ssize_t bytes_read = read_helper(file, buffer, READ_BUFFER_SIZE);

	if (bytes_read != HANGMAN_AND_SECRET_WORD_SIZE) {
		PRINT_ERR("unexpected error while reading, errno=%d", errno);
		is_success = false;
		goto close;
	}
	// we are in B state, lets write a incorrect guess character "z"
	int write_incorrect_guess_ret = write(file, "z", 1);

	if (write_incorrect_guess_ret != 1) {
		PRINT_ERR("unexpected error while writing, errno=%d", errno);
		is_success = false;
		goto close;
	}

	const char *z_revealed = "*****";
	char buffer_tmp[HANGMAN_AND_SECRET_WORD_SIZE + 1] = "";
	strcpy(buffer_tmp, z_revealed);
	buffer_tmp[SECRET_WORD_LEN] = '\n';
	strcat(buffer_tmp + SECRET_WORD_LEN + 1, hangman_empty);
	buffer_tmp[SECRET_WORD_LEN_WITH_NEWLINE + hang_man_char_index[0]] =
		'O'; // draw head
	char bf[HANGMAN_AND_SECRET_WORD_SIZE + 1] = "";
	read(file, bf, HANGMAN_AND_SECRET_WORD_SIZE);
	if (strncmp(bf, buffer_tmp, HANGMAN_AND_SECRET_WORD_SIZE) != 0) {
		PRINT_ERR("read secret word and hangman drawing are not as expected");
		is_success = false;
		goto close;
	}

close:
	close(file);
	if (is_success)
		PRINT_OK();
}

// write

/*
 * writing an invalid character outside a-z returns EINVAL.
 */
void check_write_char_outside_a_z(void)
{
	bool is_success = true;
	int file = open_file();

	if (!file) {
		return;
	}
	int ret = write(file, "1", 1);

	if (ret != -1 && errno != EINVAL) {
		PRINT_ERR("only a-z characters are allowed");
		is_success = false;
		goto close;
	}
close:
	close(file);
	if (is_success)
		PRINT_OK();
}

/*
 * writing a valid sequence of characters outside of one character returns EINVAL.
 */
void check_write_sequence_chars_outside_a_z(void)
{
	bool is_success = true;
	int file = open_file();

	if (!file) {
		return;
	}

	int ret = write(file, "abc1abc1abc", 11);

	if (ret != -1 && errno != EINVAL) {
		PRINT_ERR("only a-z characters are allowed");
		is_success = false;
		goto close;
	}
close:
	close(file);
	if (is_success)
		PRINT_OK();
}

/*
 * after entering the to-be-guessed word, a read
 * operation returns character * in a number of times
 * equal to the length of the given word.
 */
void check_secret_word_length_equal_to_insered(void)
{
	bool is_success = true;
	int file = open_file();

	if (!file) {
		return;
	}

	// we are in A state, lets write a secret word
	int write_secret_word_ret =
		pick_secret_word(file, secret_word, SECRET_WORD_LEN);

	if (!write_secret_word_ret) {
		is_success = false;
		goto close;
	}
	// now we are in B state, lets read
	char buffer[READ_BUFFER_SIZE];
	ssize_t bytes_read = read_helper(file, buffer, READ_BUFFER_SIZE);

	if (bytes_read != HANGMAN_AND_SECRET_WORD_SIZE) {
		PRINT_ERR("unexpected error while reading, errno=%d", errno);
		is_success = false;
		goto close;
	}
	buffer[SECRET_WORD_LEN_WITH_NEWLINE - 1] = '\0';
	if (strcmp(secret_word_hidden, buffer) != 0) {
		// should be "*****"
		PRINT_ERR("read secret word length or hidden content is not as expected");
		is_success = false;
		goto close;
	}

close:
	close(file);
	if (is_success)
		PRINT_OK();
}

//writing in stage C returns EINVAL
void check_write_in_C_returns_EINVAL(void)
{
	bool is_success = true;
	int file = open_file();

	if (!file)
		return;
	// we are in A state, lets write a secret word
	int write_secret_word_ret =
		pick_secret_word(file, secret_word, SECRET_WORD_LEN);

	if (!write_secret_word_ret) {
		is_success = false;
		goto close;
	}
	// now we are in B state, lets write a correct guess characters
	int write_correct_guess_ret = write(file, secret_word, SECRET_WORD_LEN);

	if (write_correct_guess_ret != 5) {
		PRINT_ERR("unexpected error while writing, errno=%d", errno);
		is_success = false;
		goto close;
	}
	// we are in C states, lets validate.
	if (check_if_in_C(file, "a") == 0) {
		PRINT_ERR("expected to get EINVAL but got errno=%d", errno);
		is_success = false;
		goto close;
	}

close:
	close(file);
	if (is_success)
		PRINT_OK();
}

/*
 * 5. after entering the to-be-guessed word, if given 8 characters,
 *   at once by write syscall, the first six are a wrong
 *   guess,the hangman is completed and no error is returned
 */
void check_finish_hangman_no_errors(void)
{
	bool is_success = true;
	int file = open_file();

	if (!file)
		return;
	// we are in A state, lets write a secret word
	int write_secret_word_ret =
		pick_secret_word(file, secret_word, SECRET_WORD_LEN);

	if (!write_secret_word_ret) {
		is_success = false;
		goto close;
	}
	// lets kill the hangman, we should not get any error
	int write_correct_guess_ret = write(file, "zzzzzzzz", 8);

	if (write_correct_guess_ret != 8) {
		PRINT_ERR("we should have not returned error," //rest in next line
			" unexpected error while writing, errno=%d", errno);
		is_success = false;
		goto close;
	}

close:
	close(file);
	if (is_success)
		PRINT_OK();
}

/*
 * after entering the to-be-guessed word, if given 5 characters, at
 * once by write syscall, the first 4 solved the entire word, the
 * word is completed and no error is returned
 */
void check_guess_word_with_extra_bad_chars_returns_no_error(void)
{
	bool is_success = true;
	int file = open_file();

	if (!file)
		return;
	// we are in A state, lets write a secret word
	int write_secret_word_ret =
		pick_secret_word(file, secret_word, SECRET_WORD_LEN);

	if (!write_secret_word_ret) {
		is_success = false;
		goto close;
	}

	int write_correct_guess_ret = write(file, "aplez", 5);

	if (write_correct_guess_ret != 4) { // your spec file said to ignore the last letter...
		PRINT_ERR("we should have not returned error," //rest in next line
			" unexpected error while writing, errno=%d", errno);
		is_success = false;
		goto close;
	}
close:
	close(file);
	if (is_success)
		PRINT_OK();
}

/*
 * attempt writing 0 bytes in state A and validate that
 * EINVAL is returned as expected.
 */
void check_write_zero_bytes_in_A_returns_EINVAL(void)
{
	bool is_success = true;
	int file = open_file();

	if (!file) {
		return;
	}
	int ret = write(file, "", 0);

	if (ret != -1 && errno != EINVAL) {
		PRINT_ERR("expected EINVAL but got errno=%d", errno);
		is_success = false;
		goto close;
	}
close:
	close(file);
	if (is_success)
		PRINT_OK();
}

/*
 * attempt writing 0 bytes in state B and validate that no
 * error is returned and the total state of the game, captured
 * by read syscall, did not change.
 */
void check_write_zero_bytes_in_B_returns_no_error(void)
{
	bool is_success = true;
	int file = open_file();

	if (!file) {
		return;
	}

	// we are in A state, lets write a secret word
	int write_secret_word_ret =
		pick_secret_word(file, secret_word, SECRET_WORD_LEN);

	if (!write_secret_word_ret) {
		is_success = false;
		goto close;
	}

	// now we are in B state, lets read
	char buffer[HANGMAN_AND_SECRET_WORD_SIZE];
	ssize_t bytes_read = read_helper(file, buffer, READ_BUFFER_SIZE);

	if (bytes_read != HANGMAN_AND_SECRET_WORD_SIZE) {
		PRINT_ERR("unexpected error while reading, errno=%d", errno);
		is_success = false;
		goto close;
	}
	char buffer_tmp[HANGMAN_AND_SECRET_WORD_SIZE];

	strcpy(buffer_tmp, "*****");
	buffer_tmp[SECRET_WORD_LEN] = '\n';
	strcat(buffer_tmp + SECRET_WORD_LEN + 1, hangman_empty);
	if (strncmp(buffer, buffer_tmp, HANGMAN_AND_SECRET_WORD_SIZE) != 0) {
		PRINT_ERR("read secret word and hangman drawing are not as expected");
		is_success = false;
		goto close;
	}
	// we are in B state, lets write 0 bytes
	int ret = write(file, "", 0);

	if (ret != 0) {
		PRINT_ERR("expected no error but got errno=%d", errno);
		is_success = false;
		goto close;
	}
	// state of game should have not changed (it should have stayed empty)
	char buffer_2[HANGMAN_AND_SECRET_WORD_SIZE];
	ssize_t bytes_read_2 = read(file, buffer_2, HANGMAN_AND_SECRET_WORD_SIZE);
	if (bytes_read_2 != HANGMAN_AND_SECRET_WORD_SIZE) {
		PRINT_ERR("unexpected error while reading, errno=%d", errno);
		is_success = false;
		goto close;
	}
	char buffer_tmp_2[READ_BUFFER_SIZE];

	strcpy(buffer_tmp_2, secret_word_hidden);
	buffer_tmp_2[SECRET_WORD_LEN] = '\n';
	strcat(buffer_tmp_2 + SECRET_WORD_LEN + 1, hangman_empty);
	if (strncmp(buffer_2, buffer_tmp_2, HANGMAN_AND_SECRET_WORD_SIZE) != 0) {
		PRINT_ERR("read secret word and hangman drawing are not as expected");
		is_success = false;
		goto close;
	}

close:
	close(file);
	if (is_success)
		PRINT_OK();
}

/*
 * attempt writing 0 bytes in state C and validate that
 * the error EINVAL was returned.
 */
void check_write_zero_in_C_returns_EINVAL(void)
{
	bool is_success = true;
	int file = open_file();

	if (!file)
		return;
	// we are in A state, lets write a secret word
	int write_secret_word_ret =
		pick_secret_word(file, secret_word, SECRET_WORD_LEN);

	if (!write_secret_word_ret) {
		is_success = false;
		goto close;
	}
	// now we are in B state, lets write a correct guess characters
	int write_correct_guess_ret = write(file, secret_word, SECRET_WORD_LEN);

	if (write_correct_guess_ret != 5) {
		PRINT_ERR("unexpected error while writing, errno=%d", errno);
		is_success = false;
		goto close;
	}
	int ret = write(file, "", 0);

	if (ret != -1 && errno != EINVAL) {
		PRINT_ERR("expected EINVAL but got errno=%d", errno);
		is_success = false;
		goto close;
	}

close:
	close(file);
	if (is_success)
		PRINT_OK();
}

// other

/*
 * run games, each opened separately and given different
 * words and guesses.
 */
void check_run_games_different_words(void)
{
	int file = open_file();

	if (!file) {
		return;
	}
	int ret = -1;

	ret = run_full_game_and_reset(file, "anakin", 6, 5);
	close(file);
	if (ret != 1) {
		return;
	}
	file = open_file();
	if (!file) {
		return;
	}
	ret = run_full_game_and_reset(file, "palpatin", 8, 8);
	close(file);
	if (ret != 1) {
		return;
	}
	file = open_file();
	if (!file) {
		return;
	}
	ret = run_full_game_and_reset(file, "kdlp", 4, 4);
	close(file);
	if (ret != 1) {
		return;
	}

	PRINT_OK();
}

/*
 * open 100 threads, each one opening and running a
 * different DI until completion of the game. This will
 * test the validity in the implementation given that
 * asynchronous calls are placed.
 */
int thread_job(void)
{
	int file = open_file();

	if (!file) {
		return -1;
	}

	int ret = run_full_game_and_reset(file, "anakin", 6, 5); //TODO: cringe

	close(file);

	if (ret != 1) {
		return -1;
	}

	return 1;
}

// Wrapper function for pthread
void *thread_function(void *arg)
{
	int *result = (int *)arg;

	*result = thread_job();
	return NULL;
}

// Function to check 100 threads
void check_100_threads(void)
{
	pthread_t threads[10];
	int results[10];

	/*
	 * Redirect stdout to /dev/null (because we want to be TAP
	 * compliant so only 1 error message is printed) - wrong shit
	 */
	int old_stdout = dup(STDOUT_FILENO);
	int dev_null = open("/dev/null", O_WRONLY);

	dup2(dev_null, STDOUT_FILENO);
	close(dev_null);

	// Create and run 10 threads
	for (int i = 0; i < 10; i++) {
		results[i] = 0;
		if (pthread_create(&threads[i], NULL, thread_function,
				   &results[i]) != 0) {
			dup2(old_stdout,
			     STDOUT_FILENO); // Restore stdout before printing the error
			close(old_stdout);
			PRINT_ERR("Error creating thread %d\n", i);
			return;
		}
	}

	// Wait for all threads to finish
	for (int i = 0; i < 10; i++) {
		pthread_join(threads[i], NULL);
	}

	// Restore stdout after threads have completed
	dup2(old_stdout, STDOUT_FILENO);
	close(old_stdout);

	// Check the results
	for (int i = 0; i < 10; i++) {
		if (results[i] != 1) {
			PRINT_ERR("Error: Thread %d did not return success\n", i);
			return;
		}
	}
}

//run a game into completion 20 times, doing a reset in between.
void check_run_game_20_times(void)
{
	int file = open_file();

	if (!file) {
		return;
	}
	for (int i = 0; i < 20; i++) {
		int ret = run_full_game_and_reset(file, "anakin", 6, 5);

		if (ret != 1) {
			return;
		}
	}
	close(file);
	PRINT_OK();
}

void (*test_ptrs[NUM_TESTS])(void) = {
	check_null_dereference,
	check_invaild_buffer_address,
	check_file_permissions,
	check_invaild_ioctl,
	check_reset_in_A,
	check_reset_in_B,
	check_reset_in_C,
	check_bad_syscall_then_reset,
	check_read_in_A,
	check_read_in_B,
	check_correct_character_not_advance_figure,
	check_multiple_occurrences_of_correct_character,
	check_incorrect_character_advance_figure,
	check_write_char_outside_a_z,
	check_write_sequence_chars_outside_a_z,
	check_finish_hangman_no_errors,
	check_guess_word_with_extra_bad_chars_returns_no_error,
	check_write_zero_bytes_in_A_returns_EINVAL,
	check_write_zero_bytes_in_B_returns_no_error,
	check_write_zero_in_C_returns_EINVAL,
	check_write_in_C_returns_EINVAL,
	check_secret_word_length_equal_to_insered,
	check_run_games_different_words,
	check_100_threads,
	check_run_game_20_times,
};

int main(void)
{
	printf("1..%d\n", NUM_TESTS);

	for (int i = 0; i < NUM_TESTS; i++) {
		current_func_num = i + 1;
		HOOK_AND_INVOKE(i, test_ptrs);
	}
}
