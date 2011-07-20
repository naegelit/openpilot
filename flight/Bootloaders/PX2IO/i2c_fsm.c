/**
 * I2C state machine for the PX2IO bootloader.
 */

#include <pios.h>

#include <stm32f10x_flash.h>
#include <stm32f10x_i2c.h>

#include <bl_fsm.h>
#include <dcc_stdio.h>

#include <string.h>


/*
 * I2C protocol
 *
 * Read always returns:
 *  - status byte
 *   'o' - last command completed successfully
 *   'e' - last command completed with an error
 *  - buffer bytes to max buffer size
 *  - pad bytes (0xff)
 *
 * Write:
 *  - command byte
 *  - argument byte(s)
 *
 * Commands:
 * <addr> is an address of up to 4 bytes, LSB-first.  Bytes not sent are treated as zero.
 * <data> is up to FLASH_PAGE_SIZE bytes of data.  Only the bytes sent are programmed.
 *
 * <'w'><data...>
 * 	Writes data to the buffer.
 *
 * <'a'><addr>
 *  Sets the buffer address at which writing starts.
 *  <addr> buffer offset (0 ... (FLASH_PAGE_SIZE -1))
 *
 * <'e'>
 *  Erases the program area and prepares for programming.
 *
 * <'f'><addr>
 *  Flashes the contents of the buffer to flash address <addr>
 *  <addr> 4-byte memory address
 *
 * <'c'>
 *  Calculates the 4-byte CRC of the program space and places it in the buffer.
 *
 * <'s'>
 *  Places the TBD byte serial number of the board in the buffer.
 *
 * <'b'>
 *  Reboots the board.
 *
 * <'r'><addr>
 *  Copies FLASH_PAGE_SIZE bytes of memory from <addr> to the buffer
 *  <addr> 4-byte memory address
 *
 * <'m'>
 *  Places the memory map structure (TBD, likely to be like the AHRS memory map) in the buffer.
 *
 * <'v'>
 *  Places the versions structure (TBD, likely to be the same as the AHRS) in the buffer.
 *
 */

/**
 * States implemented by the I2C FSM.
 */
enum fsm_state {
	BAD_PHASE,			// must be zero, default exit on a bad state transition

	WAIT_FOR_MASTER, //!< WAIT_FOR_MASTER

	/* write from master */
	WAIT_FOR_COMMAND,//!< WAIT_FOR_COMMAND
	RECEIVE_COMMAND, //!< RECEIVE_COMMAND
	RECEIVE_DATA,    //!< RECEIVE_DATA
	HANDLE_COMMAND,  //!< HANDLE_COMMAND

	/* read from master */
	WAIT_TO_SEND,    //!< WAIT_TO_SEND
	SEND_STATUS,     //!< SEND_STATUS
	SEND_DATA,       //!< SEND_DATA
	NUM_STATES       //!< NUM_STATES
};

/**
 * Events recognised by the I2C FSM.
 */
enum fsm_event {
	/* automatic transition */
	AUTO,           //!< AUTO

	/* write from master */
	ADDRESSED_WRITE,//!< ADDRESSED_WRITE
	BYTE_RECEIVED,  //!< BYTE_RECEIVED
	STOP_RECEIVED,  //!< STOP_RECEIVED

	/* read from master */
	ADDRESSED_READ, //!< ADDRESSED_READ
	BYTE_SENDABLE,  //!< BYTE_SENDABLE
	ACK_FAILED,     //!< ACK_FAILED

	NUM_EVENTS      //!< NUM_EVENTS
};

#define STATUS_OK				'o'
#define STATUS_RANGE_ERROR		'e'
#define STATUS_COMMAND_ERROR	'c'
#define STATUS_COMMAND_FAILED	'f'
#define STATUS_DATA_ERROR		'd'

#define FLASH_PAGE_SIZE	1024	// XXX 2048 for high-density devices, should config automatically

/**
 * Context for the I2C FSM
 */
struct fsm_context {
	I2C_TypeDef		*regs;
	enum fsm_state	state;

	uint8_t			status;
	uint8_t			command;
	uint8_t			*data_ptr;
	uint32_t		data_count;

	uint32_t		address;
	uint8_t			buffer[FLASH_PAGE_SIZE];
};

/**
 * Structure defining one FSM state and its outgoing transitions.
 */
struct fsm_transition {
		void (*handler)(struct fsm_context *ctx);
		enum fsm_state next_state[NUM_EVENTS];
};

static void fsm_event(struct fsm_context *ctx, enum fsm_event event);

static void	go_bad(struct fsm_context *ctx);
static void	go_wait_master(struct fsm_context *ctx);

static void	go_wait_command(struct fsm_context *ctx);
static void	go_receive_command(struct fsm_context *ctx);
static void	go_receive_data(struct fsm_context *ctx);
static void	go_handle_command(struct fsm_context *ctx);

static void	go_wait_send(struct fsm_context *ctx);
static void	go_send_status(struct fsm_context *ctx);
static void	go_send_buffer(struct fsm_context *ctx);

/**
 * The FSM state graph.
 */
struct fsm_transition fsm[NUM_STATES] = {
		[BAD_PHASE] = {
				.handler = go_bad,
				.next_state = {
						[AUTO] = WAIT_FOR_MASTER,
				},
		},

		[WAIT_FOR_MASTER] = {
				.handler = go_wait_master,
				.next_state = {
						[ADDRESSED_WRITE] = WAIT_FOR_COMMAND,
						[ADDRESSED_READ] = WAIT_TO_SEND,
				},
		},

		/* write from master*/
		[WAIT_FOR_COMMAND] = {
				.handler = go_wait_command,
				.next_state = {
						[BYTE_RECEIVED] = RECEIVE_COMMAND,
						[STOP_RECEIVED] = HANDLE_COMMAND,
				},
		},
		[RECEIVE_COMMAND] = {
				.handler = go_receive_command,
				.next_state = {
						[BYTE_RECEIVED] = RECEIVE_DATA,
						[STOP_RECEIVED] = WAIT_FOR_MASTER,
				},
		},
		[RECEIVE_DATA] = {
				.handler = go_receive_data,
				.next_state = {
						[BYTE_RECEIVED] = RECEIVE_DATA,
						[STOP_RECEIVED] = WAIT_FOR_MASTER,
				},
		},
		[HANDLE_COMMAND] = {
				.handler = go_handle_command,
				.next_state = {
						[AUTO] = WAIT_FOR_MASTER,
				},
		},

		/* buffer send */
		[WAIT_TO_SEND] = {
			.handler = go_wait_send,
			.next_state = {
					[BYTE_SENDABLE] = SEND_STATUS,
			},
		},
		[SEND_STATUS] = {
				.handler = go_send_status,
				.next_state = {
						[BYTE_SENDABLE] = SEND_DATA,
						[ACK_FAILED] = WAIT_FOR_MASTER,
				},
		},
		[SEND_DATA] = {
				.handler = go_send_buffer,
				.next_state = {
						[BYTE_SENDABLE] = SEND_DATA,
						[ACK_FAILED] = WAIT_FOR_MASTER,
				},
		},
};

static struct fsm_context context;

/* debug support */

struct fsm_logentry {
	char		kind;
	uint32_t	code;
};

#define LOG_ENTRIES	32
static struct fsm_logentry fsm_log[LOG_ENTRIES];
int	fsm_logptr;
#define LOG_NEXT(_x)	(((_x) + 1) % LOG_ENTRIES)
#define LOGx(_kind, _code)		\
		do {					\
			fsm_log[fsm_logptr].kind = _kind; \
			fsm_log[fsm_logptr].code = _code; \
			fsm_logptr = LOG_NEXT(fsm_logptr); \
			fsm_log[fsm_logptr].kind = 0; \
		} while(0)

#define LOG(_kind, _code) \
do {\
	if (fsm_logptr < LOG_ENTRIES) { \
		fsm_log[fsm_logptr].kind = _kind; \
		fsm_log[fsm_logptr].code = _code; \
		fsm_logptr++;\
	}\
}while(0)

/**
 * Attach the FSM to an I2C port.
 *
 * @param regs	port register pointer
 */
void
i2c_fsm_attach(I2C_TypeDef *regs)
{
	context.regs = regs;
}

/**
 * run the I2C FSM
 */
void
i2c_fsm()
{
	uint32_t			event;
	struct fsm_context	*ctx;

	// initialise the FSM
	ctx = &context;
	ctx->state = WAIT_FOR_MASTER;
	ctx->status = STATUS_OK;

	dbg_write_str("fsm");

	// spin handling I2C events
	for (;;) {

		// handle bus error states by discarding the current operation
		if (I2C_GetFlagStatus(ctx->regs, I2C_FLAG_BERR)) {
			ctx->state = WAIT_FOR_MASTER;
			I2C_ClearFlag(ctx->regs, I2C_FLAG_BERR);
		}

		// we do not anticipate over/underrun errors as clock-stretching is enabled

		// fetch the most recent event
		event = I2C_GetLastEvent(I2C1);

		// generate FSM events based on I2C events
		switch (event) {
		case I2C_EVENT_SLAVE_RECEIVER_ADDRESS_MATCHED:
			LOG('w', 0);
			fsm_event(ctx, ADDRESSED_WRITE);
			break;

		case I2C_EVENT_SLAVE_TRANSMITTER_ADDRESS_MATCHED:
			LOG('r', 0);
			fsm_event(ctx, ADDRESSED_READ);
			break;

		case I2C_EVENT_SLAVE_BYTE_RECEIVED:
			LOG('R', 0);
			fsm_event(ctx, BYTE_RECEIVED);
			break;

		case I2C_EVENT_SLAVE_STOP_DETECTED:
			LOG('s', 0);
			fsm_event(ctx, STOP_RECEIVED);
			break;

		case I2C_EVENT_SLAVE_BYTE_TRANSMITTING:
		//case I2C_EVENT_SLAVE_BYTE_TRANSMITTED:
			LOG('T', 0);
			fsm_event(ctx, BYTE_SENDABLE);
			break;

		case I2C_EVENT_SLAVE_ACK_FAILURE:
			LOG('a', 0);
			fsm_event(ctx, ACK_FAILED);
			break;

		default:
//			if ((event) && (event != 0x00020000))
//				LOG('e', event);
			break;
		}
	}
}

/**
 * Update the FSM with an event
 *
 * @param ctx	FSM context.
 * @param event	New event.
 */
static void
fsm_event(struct fsm_context *ctx, enum fsm_event event)
{
	// move to the next state
	ctx->state = fsm[ctx->state].next_state[event];

	LOG('f', ctx->state);

	// call the state entry handler
	if (fsm[ctx->state].handler) {
		fsm[ctx->state].handler(ctx);
	}
}

static void
go_bad(struct fsm_context *ctx)
{
	dbg_write_str("go_bad");

	fsm_event(ctx, AUTO);
}

/**
 * Wait for the master to address us.
 *
 * @param ctx	FSM context.
 */
static void
go_wait_master(struct fsm_context *ctx)
{
	ctx->command = ' ';
	ctx->data_ptr = 0;
	ctx->data_count = 0;

	// (re)enable the peripheral, clear the stop event flag in
	// case we just finished receiving data
	I2C_Cmd(ctx->regs, ENABLE);

	// clear the ACK failed flag in case we just finished sending data
	I2C_ClearFlag(ctx->regs, I2C_FLAG_AF);
}

/**
 * Prepare to receive a command byte.
 *
 * @param ctx	FSM context.
 */
static void
go_wait_command(struct fsm_context *ctx)
{
	// NOP
}

/**
 * Command byte has been received, do setup required for command arguments.
 *
 * @param ctx	FSM context.
 */
static void
go_receive_command(struct fsm_context *ctx)
{

	// fetch the command byte
	ctx->command = I2C_ReceiveData(ctx->regs);
	LOG('c', ctx->command);

	switch (ctx->command) {
	case 'a':
	case 'f':
	case 'r':
		// set up to receive an address
		ctx->address = 0;
		ctx->data_ptr = (uint8_t *)&(ctx->address);
		ctx->data_count = sizeof(ctx->address);
		break;

	case 'w':
		// set up to receive a page of data
		ctx->data_ptr = ctx->buffer;
		ctx->data_count = sizeof(ctx->buffer);
		break;

	default:
		break;
	}
}

/**
 * Receive a data byte.
 *
 * @param ctx	FSM context.
 */
static void
go_receive_data(struct fsm_context *ctx)
{
	uint8_t	d;

	// fetch the byte
	d = I2C_ReceiveData(ctx->regs);
	LOG('d', d);

	// if we have somewhere to put it, do so
	if (ctx->data_count) {
		*ctx->data_ptr++ = d;
		ctx->data_count--;
	}
}

/**
 * Handle a command once the host is done sending it to us.
 *
 * @param ctx	FSM context.
 */
static void
go_handle_command(struct fsm_context *ctx)
{
	uint32_t	crc;

	// presume we are happy with the command
	ctx->status = STATUS_OK;

	switch (ctx->command) {
	case 'a':
		// range-check the buffer address
		if (ctx->address >= FLASH_PAGE_SIZE) {
			ctx->status = STATUS_RANGE_ERROR;
		}
		break;

	case 'b':
		// re-lock the flash and start the app
		// XXX should probably just turn off boot-to-bootloader and reset
		FLASH_Lock();
		jump_to_app();

		// we decided not to jump...
		ctx->status = STATUS_COMMAND_FAILED;
		break;

	case 'c':
		// Generate CRC for the app area and write it to the buffer
		crc = PIOS_BL_HELPER_CRC_Memory_Calc();
		memcpy(ctx->buffer, &crc, sizeof(crc));
		break;

	case 'e':
		// unlock and erase the flash
		PIOS_BL_HELPER_FLASH_Ini();
		if (!PIOS_BL_HELPER_FLASH_Start()) {
			ctx->status = STATUS_COMMAND_FAILED;
		}
		break;

	case 'f':
		// program the buffer at the supplied address
		for (int i = 0; (i + ctx->data_count + 3) < FLASH_PAGE_SIZE; i += 4) {
			if (FLASH_COMPLETE != FLASH_ProgramWord(ctx->address + i, *(uint32_t *)(ctx->buffer + i))) {
				ctx->status = STATUS_COMMAND_FAILED;
				break;
			}
		}
		break;

	case 'm':
		// XXX copy memory map to buffer
		break;

	case 'r':
		// fill the buffer with flash data from the supplied address
		memcpy(ctx->buffer, (void *)(ctx->address), FLASH_PAGE_SIZE);
		break;

	case 's':
		// Copy serial number to the buffer
		// zero-fill, terminate, etc?
		PIOS_SYS_SerialNumberGet((char *)&(ctx->buffer[0]));
		break;

	case 'v':
		// XXX copy versions to buffer
		break;

	case 'w':
		// No action required
		break;

	default:
		ctx->status = STATUS_COMMAND_ERROR;
		break;
	}

	// kick along to the next state
	fsm_event(ctx, AUTO);
}

/**
 * Wait to be able to send the status byte.
 *
 * @param ctx	FSM context.
 */
static void
go_wait_send(struct fsm_context *ctx)
{
	ctx->data_ptr = ctx->buffer;
	ctx->data_count = sizeof(ctx->buffer);
}

/**
 * Send the status byte.
 *
 * @param ctx	FSM context.
 */
static void
go_send_status(struct fsm_context *ctx)
{
	I2C_SendData(ctx->regs, ctx->status);
	LOG('?', ctx->status);
}

/**
 * Send a data or pad byte.
 *
 * @param ctx	FSM context.
 */
static void
go_send_buffer(struct fsm_context *ctx)
{
	if (ctx->data_count) {
		LOG('D', *ctx->data_ptr);
		I2C_SendData(ctx->regs, *(ctx->data_ptr++));
		ctx->data_count--;
	} else {
		LOG('-', 0);
		I2C_SendData(ctx->regs, 0xff);
	}
}
