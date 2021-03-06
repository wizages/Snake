/**************************************************************************
 * File:         app.c       Multitasking Pong game running under uCOS-III RTOS
 * Date:         September 12, 2013
 * Status:       Some bugs remaining but generally functions.
 * Processor:	 PIC32MX795F512L
 * Toolchain:	 MPLAB-X
 * Board:        MAX32 from Digilent
 * Programmer:	 M.J. Batchelder
 * Organization: CENG 448/548, SDSMT
 * RTOS:         uCOS-III from Micrium
 * Description:	PONG game using TeraTerm or PuTTY as VT100 terminal for display
 *		Left and right players have up and down buttons as input
 *       VT100 control codes move the cursor to position the paddles
 *		and ball.  Each side has a score advanced when the ball hits
 *		the opposite wall.
 * Tasks: Ball task, Left Paddle Task, Right Paddle Task, 
 *        Start task with Blink LED Task, idle task, timer task,
 *        Optional tasks enabled in os_cfg.h: statistics task, stack check task
 *		
 *
 * Notes on VT100 emulation
 * 	Clear Screen:	esc c			0x1B 0x63
 *	Move Cursor:	esc [ row ; col H	0x1B 0x3B rr 0x1B cc 0x28
 *        for example: 	move to row 12 col 13 -- rr = 0x31 0x32, cc = 0x31 0x33
 *			move to row 2 col 3 -- rr = 0x30 0x32 or rr = 0x32
 *           cc = 0x30 0x33 or cc = 0x33
 * Notes: 
 *       bsp.c holds pragma config 
 *       bsp.c in BSP_InitIO has SYSTEMConfigPerformance(BSP_CLK_FREQ); and mOSCSetPBDIV(0);
 *       bsp.h holds BSP_CLK_FREQ 80,000,000
 *       bsp.h holds I/O definitions for MAX32 using Basic Shield
 *             including buttons, switches, and LEDs 
 *       uart1.h holds BAUD_RATE 115200
 * 
 *      os_cfg.h enables/disables inclusion of  statistics task 
 **************************************************************************/
#include <includes.h>

/*
 *******************************************************************************
 *                                                Task Stacks/TCB
 *******************************************************************************
 */

#define TASK_STK_SIZE 512

#define APP_CFG_TASK_BALL_STK_SIZE              TASK_STK_SIZE
#define APP_CFG_TASK_PADDLE_LEFT_STK_SIZE       TASK_STK_SIZE
#define APP_CFG_TASK_PADDLE_RIGHT_STK_SIZE      TASK_STK_SIZE


#define  APP_CFG_TASK_STK_SIZE_PCT_FULL             90u
#define APP_CFG_TASK_BALL_STK_SIZE_LIMIT            (TASK_STK_SIZE* (100u - APP_CFG_TASK_STK_SIZE_PCT_FULL)) / 100u
#define APP_CFG_TASK_PADDLE_LEFT_STK_SIZE_LIMIT     (TASK_STK_SIZE* (100u - APP_CFG_TASK_STK_SIZE_PCT_FULL)) / 100u
#define APP_CFG_TASK_PADDLE_RIGHT_STK_SIZE_LIMIT    (TASK_STK_SIZE* (100u - APP_CFG_TASK_STK_SIZE_PCT_FULL)) / 100u

static OS_TCB App_TaskStartTCB;
static CPU_STK App_TaskStartStk[APP_CFG_TASK_START_STK_SIZE];

static OS_TCB  App_TaskBallTCB;
static CPU_STK App_TaskBallStk[APP_CFG_TASK_BALL_STK_SIZE];

static OS_TCB App_TaskPaddleLeftTCB;
static CPU_STK App_TaskPaddleLeftStk[APP_CFG_TASK_PADDLE_LEFT_STK_SIZE];

static OS_TCB App_TaskPaddleRightTCB;
static CPU_STK App_TaskPaddleRightStk[APP_CFG_TASK_PADDLE_RIGHT_STK_SIZE];

/*
 *******************************************************************************
 *                                            Task PROTOTYPES
 *******************************************************************************
 */

static void App_TaskStart(void *p_arg);
static void AppTaskCreate(void);

static void App_TaskBall(void *data);
static void App_TaskPaddleLeft(void *data);
static void App_TaskPaddleRight(void *data);

/*
 *******************************************************************************
 *                                            Pong Task Priorities
 *******************************************************************************
 */

#define APP_CFG_TASK_BALL_PRIO          4
#define APP_CFG_TASK_PADDLE_LEFT_PRIO   5
#define APP_CFG_TASK_PADDLE_RIGHT_PRIO  6

// ***************************************************************************
// GPIO defines     Switches on Digilent Basic Shield 
// declared in bsp.h
// ***************************************************************************

#define RIGHT_UP_SW     BTN1 
#define RIGHT_DOWN_SW   BTN2 
#define LEFT_UP_SW      BTN3 
#define LEFT_DOWN_SW	BTN4 


// ***************************************************************************
// Screen defines and function prototypes
// ***************************************************************************
#define SCREEN_X_START      1
#define SCREEN_X_END        80
#define SCREEN_Y_START      1
#define SCREEN_Y_END        25

#define PADDLE_LEFT_X       2
#define PADDLE_RIGHT_X      (SCREEN_X_END - 2)
#define PADDLE_Y_START	    10
#define PADDLE_LENGTH	    6

#define BALL_X_START	    ((SCREEN_X_END - SCREEN_X_START)/2)
#define BALL_Y_START	    ((SCREEN_Y_END - SCREEN_Y_START)/2)
#define SCORE_LEFT_X_START   3
#define SCORE_Y              3
#define SCORE_RIGHT_X_START  (SCREEN_X_END - 5)

void Screen_Init(void);
void Screen_Clear(void);
void Screen_MoveCursor(int Xpos, int Ypos);
int Screen_WriteChar(int x, int y, char c);
int Screen_WriteNumber(int x, int y, int number);
void Screen_OffCursor(void);
void Screen_OnCursor(void);

// ********************************************************************* */
// Global Variables
// *********************************************************************
int x_delta, y_delta;

/*
 *********************************************************************************************************
 *                                                main()
 *
 * Description : This is the standard entry point for C code.
 *
 * Arguments   : none
 *********************************************************************************************************
 */

int main(void) {

    OS_ERR os_err;

    CPU_Init(); /* Initialize the uC/CPU services  */

    BSP_IntDisAll();

    OSInit(&os_err); /* Init uC/OS-III.                  */

    // app_cfg.h holds priority, stack size, and limit for start task
    OSTaskCreate((OS_TCB *) & App_TaskStartTCB, /* Create the start task            */
            (CPU_CHAR *) "Start",
            (OS_TASK_PTR) App_TaskStart,
            (void *) 0,
            (OS_PRIO) APP_CFG_TASK_START_PRIO,
            (CPU_STK *) & App_TaskStartStk[0],
            (CPU_STK_SIZE) APP_CFG_TASK_START_STK_SIZE_LIMIT,
            (CPU_STK_SIZE) APP_CFG_TASK_START_STK_SIZE,
            (OS_MSG_QTY) 0u,
            (OS_TICK) 0u,
            (void *) 0,
            (OS_OPT) (OS_OPT_TASK_STK_CHK | OS_OPT_TASK_STK_CLR),
            (OS_ERR *) & os_err);

    OSStart(&os_err); /* Start multitasking (i.e. give control to uC/OS-III). */
    /* Should not return here as RTOS takes control */
    (void) &os_err;

    return (0);
}

/*$PAGE*/

/*
 *********************************************************************************************************
 *                                          STARTUP TASK
 *
 * Description : This is an example of a startup task.  
 * Arguments   : p_arg   is the argument passed to 'AppStartTask()' by 'OSTaskCreate()'.
 *********************************************************************************************************
 */

static void App_TaskStart(void *p_arg) {
#if CPU_CFG_CRITICAL_METHOD == CPU_CRITICAL_METHOD_STATUS_LOCAL // maybe not needed for uCOS-III
    CPU_SR cpu_sr;
#endif
    CPU_INT08U i;
    CPU_INT08U j;

    (void) p_arg;
    OS_ERR err;

    BSP_InitIO(); /* Initialize BSP functions                                 */

#if OS_CFG_STAT_TASK_EN > 0  // Set in os_cfg.h
    OSStatTaskCPUUsageInit(&err);
    if (err != OS_ERR_NONE) {
        putsU1("Error starting OSStatTaskCPUUsageInit ");
    }
#endif

    initU1(); // Initialize UART1

    AppTaskCreate(); /* Create application tasks                                 */

    // ----- Task continues executing as infinite loop -----------
    // ----- writing statistics to screen and blinking LEDs ------
    while (DEF_TRUE) { /* Task body, always written as an infinite loop.            */

// If enabled writes:  CPU usage, number of tasks, stack free, stack used
#if OS_CFG_STAT_TASK_EN > 0 // Set in os_cfg.h
        OSSchedLock(&err); // Don't let another task run as it might change cursor position
        Screen_MoveCursor(40, 2); // before  writing to the screen
        UART_PrintNum(OSStatTaskCPUUsage);
        Screen_MoveCursor(40, 3); // before  writing to the screen
        UART_PrintNum(OSTaskQty);
        Screen_MoveCursor(40, 4); // before  writing to the screen
        UART_PrintNum3(App_TaskStartTCB.StkFree);
        Screen_MoveCursor(40, 5); // before  writing to the screen
        UART_PrintNum3(App_TaskStartTCB.StkUsed);
        OSSchedUnlock(&err); // Ok for other tasks to run
#endif

        for (i = 1; i < 9; i++) {
            LED_Toggle(i);
            OSTimeDlyHMSM(0, 0, 0, 500, OS_OPT_TIME_HMSM_STRICT, &err);
        }

    }
}

/*$PAGE*/

/*
 ******************************************************************************
 *                                        CREATE APPLICATION TASKS
 *
 * Description: This function creates the application tasks.
 *
 * Arguments  : none.
 *
 * Note(s)    : none.
 ******************************************************************************
 */

static void AppTaskCreate(void) {

    OS_ERR os_err, err;
    unsigned char i;
    char key;

    Screen_Init();

    OSTimeDlyHMSM(0, 0, 2, 0, OS_OPT_TIME_HMSM_STRICT, &err); /* Wait two seconds */
    if (err != OS_ERR_NONE) {
        putsU1("Error OSTimeDlyHMSM ");
    }

    Screen_MoveCursor(8, 8);
    putsU1("uC/OS-III, The Real-Time Kernel PIC32 \n"
            " PONG\n");

    OSTimeDlyHMSM(0, 0, 2, 0, OS_OPT_TIME_HMSM_STRICT, &err); /* Wait two seconds */
    if (err != OS_ERR_NONE) {
        putsU1("Error OSTimeDlyHMSM ");
    }
    Screen_Init();
    Screen_OffCursor();

    OSTaskCreate((OS_TCB *) & App_TaskBallTCB, /* Create the ball task       */
            (CPU_CHAR *) "Ball",
            (OS_TASK_PTR) App_TaskBall,
            (void *) 0,
            (OS_PRIO) APP_CFG_TASK_BALL_PRIO,
            (CPU_STK *) & App_TaskBallStk[0],
            (CPU_STK_SIZE) APP_CFG_TASK_BALL_STK_SIZE_LIMIT,
            (CPU_STK_SIZE) APP_CFG_TASK_BALL_STK_SIZE,
            (OS_MSG_QTY) 0u,
            (OS_TICK) 0u,
            (void *) 0,
            (OS_OPT) (OS_OPT_TASK_STK_CHK | OS_OPT_TASK_STK_CLR),
            (OS_ERR *) & os_err);

    if (os_err != OS_ERR_NONE) {
        putsU1("Error starting Ball task: ");
    }

}


/*$PAGE*/


/* ********************************************************************* */
/* Local functions */
// *********************************************************************

// Prints a 2 digit base 10 number

void UART_PrintNum(int i) {
    putU1(i / 10u + '0');
    putU1(i % 10u + '0');
}

// Prints a 3 digit base 10 number

void UART_PrintNum3(int i) {
    putU1(i / 100u + '0');
    putU1((i % 100) / 10u + '0');
    putU1(((i % 100) % 10) + '0');
}

void Screen_Clear(void) {
    putU1(0x1B);
    putU1('c');
}

void Screen_OffCursor(void) // Does seem to work
{
    putU1(0x1B);
    putU1('[');
    putU1('?');
    putU1('2');
    putU1('5');
    putU1('l');
}

void Screen_OnCursor(void) {
    putU1(0x1B);
    putU1('[');
    putU1('?');
    putU1('2');
    putU1('5');
    putU1('h');
}

void Screen_MoveCursor(int Xpos, int Ypos) {
    putU1(0x1B);
    putU1('[');
    UART_PrintNum(Ypos);
    putU1(';');
    UART_PrintNum(Xpos);
    putU1('H');
}

int Screen_WriteChar(int x, int y, char c) {
    OS_ERR err;
    if ((x > SCREEN_X_END) || (x < SCREEN_X_START) ||
            (y > SCREEN_Y_END) || (y < SCREEN_Y_START)) {
        return 1; // Error
    }
    OSSchedLock(&err); // Don't let another task run as it might change cursor position
    Screen_MoveCursor(x, y); // before the character is written to the screen
    putU1(c);
    OSSchedUnlock(&err); // Ok for other tasks to run
}

int Screen_WriteNumber(int x, int y, int number) {
    OS_ERR err;
    if ((x > SCREEN_X_END) || (x < SCREEN_X_START) ||
            (y > SCREEN_Y_END) || (y < SCREEN_Y_START)) {
        return 1; // Error
    }
    OSSchedLock(&err); // Don't let another task run as it might change cursor position
    Screen_MoveCursor(x, y); // before writing the value to the screen
    UART_PrintNum(number);
    OSSchedUnlock(&err); // Ok for other tasks to run
}

void Screen_Init(void) {
    int i;
    Screen_OffCursor();
    Screen_Clear();
    Screen_OffCursor();
}


/* ********************************************************************* */
/* Application Tasks*/
// *********************************************************************

void App_TaskBall(void *data) {
    int x, y;
    x_delta = 1;
    y_delta = 0;
    //int Ls, Rs, Left_Score = 0, Right_Score = 0;
    OS_ERR err;

    //Screen_WriteNumber(SCORE_LEFT_X_START, SCORE_Y, Left_Score);
    //Screen_WriteNumber(SCORE_RIGHT_X_START, SCORE_Y, Right_Score);

    x = BALL_X_START;
    y = BALL_Y_START;
    Screen_WriteChar(x, y, '*');
    while (DEF_TRUE) {
        OSTimeDlyHMSM(0, 0, 0, 75, OS_OPT_TIME_HMSM_STRICT, &err); // Wait -- give another task a chance to run
        Screen_WriteChar(x, y, ' '); // Erase old position

        //if ((Ls = (x > SCREEN_X_END)) || (Rs = (x < SCREEN_X_START))) Ball_Xdelta = -Ball_Xdelta; //Provides collisions
        //if ((y > SCREEN_Y_END) || (y < SCREEN_Y_START)) Ball_Ydelta = -Ball_Ydelta; //Provides collisions
        if (Ball_Up())
        {
            x_delta = 0;
            y_delta = 1;
        } else if (Ball_Down())
        {
            x_delta = 0;
            y_delta = -1;
        } else if (Ball_Right())
        {
            x_delta = -1;
            y_delta = 0;
        } else if (Ball_Left())
        {
            x_delta = 1;
            y_delta = 0;
        }

        x += x_delta; // Move to new position
        y += y_delta;
        Screen_WriteChar(x, y, '*');
    }
}

int Ball_Up() {
    if (RIGHT_UP_SW)
        return 1;
    else
        return 0;
}

int Ball_Down(void) {
    if (RIGHT_DOWN_SW)
        return 1;
    else
        return 0;
}

int Ball_Left() {
    if (LEFT_UP_SW)
        return 1;
    else
        return 0;
}

int Ball_Right() {
    if (LEFT_DOWN_SW)
        return 1;
    else
        return 0;
}