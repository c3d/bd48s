/*
 * Copyright (c) 2014-2015, Claudio Lapilli and the newRPL Team
 * All rights reserved.
 * This file is released under the 3-clause BSD license.
 * See the file LICENSE.txt that shipped with this distribution.
 */

// THIS IS THE MAIN STABLE API FOR KEYBOARD ACCESS
// KEYBOARD RESPONSE IS CUSTOMIZED FOR HP PRIME G1 TARGET

#include <cmdcodes.h>
#include <fsystem.h>
#include <libraries.h>
#include <newrpl.h>
#include <sysvars.h>
#include <ui.h>

static inline word_p rplDecompileAnyway(word_p object, int32_t flags)
{
    int32_t SavedException = Exceptions;
    int32_t SavedErrorCode = ErrorCode;

    Exceptions = 0;     // ERASE ANY PREVIOUS ERROR TO ALLOW THE DECOMPILER TO RUN
    // DO NOT SAVE IPtr BECAUSE IT CAN MOVE
    word_p opname = rplDecompile(object, flags);

    Exceptions = SavedException;
    ErrorCode = SavedErrorCode;

    return opname;
}

// Sets pointers to string. Returns string length in code points
static int32_t rplGetStringPointers(word_p object, byte_p *start, byte_p *end)
{
    *start = (byte_p) (object + 1);
    int32_t totaln = rplStrLenCp(object);
    *end = (byte_p) utf8nskip((char *)*start, (char *)rplSkipOb(object), totaln);
    return totaln;
}

// Decompiles object and sets pointers to resulting string
// Returns 0 on error with target pointers set to null
// Returns string length in code points if ok with target pointers set
static int32_t rplGetDecompiledString(word_p object, int32_t flags, byte_p *start, byte_p *end)
{
    word_p opname = rplDecompileAnyway(object, flags);
    if (!opname) {
        *start = NULL;
        *end = NULL;
        return 0;
    }

    return rplGetStringPointers(opname, start, end);
}

// Decompiles object and sets pointers to resulting string with tickmarks removed
// returns 0 on error with target pointers set to null
// returns 1 if ok with target pointers set
static int rplGetDecompiledStringWithoutTickmarks(word_p object, int32_t flags, byte_p *start, byte_p *end)
{
    int32_t totaln = rplGetDecompiledString(object, flags, start, end);
    if (!totaln)
        return 0;

    // IN ALGEBRAIC MODE, REMOVE THE TICK MARKS AND INSERT WITHOUT SEPARATION
    // TO ALLOW PASTING EQUATIONS INTO OTHER EXPRESSIONS
    if((totaln > 2) && ((*start)[0] == '\'')) {
        ++(*start);
        --(*end);
    }

    return 1;
}

// WAITS FOR A KEY TO BE PRESSED IN SLOW MODE

int32_t halWaitForKey()
{
    unsigned keymsg;
    int wokeup;

    if(!(halFlags & HAL_FASTMODE) && (halBusyEvent >= 0)) {
        tmr_eventkill(halBusyEvent);
        halBusyEvent = -1;
    }

    wokeup = 0;
    do {

        keymsg = keyb_getmsg();

        if(!keymsg) {
            // FIRST: ENTER LOW SPEED MODE
            // UPDATE RESPONSIVENESS FLAG
            if(rplTestSystemFlag(FL_QUICKRESPONSE))
                halFlags |= HAL_QUICKRESPONSE;
            else
                halFlags &= ~HAL_QUICKRESPONSE;

            if(halFlags & HAL_FASTMODE) {
                halCPUSlowMode();
                halFlags &= ~HAL_FASTMODE;
            }
            if(halFlags & HAL_HOURGLASS) {
                halSetNotification(N_HOURGLASS, ggl_color(PAL_GRAY0));
                halFlags &= ~HAL_HOURGLASS;
                halScreenUpdated();
            }

            if(!keyb_wasupdated() && wokeup)
                return 0;       // ALLOW SCREEN REFRESH REQUESTED BY OTHER IRQ'S

            // LAST: GO INTO "WAIT FOR INTERRUPT"
            cpu_waitforinterrupt();
            wokeup = 1;
        }
    }
    while(!keymsg);

    return keymsg;

}

// DO-NOTHING HANDLER
void timeouthandler()
{
    halFlags |= HAL_TIMEOUT;
}

int32_t halWaitForKeyTimeout(int32_t timeoutms)
{
    unsigned keymsg;
    int wokeup;

    if(!(halFlags & HAL_FASTMODE) && (halBusyEvent >= 0)) {
        tmr_eventkill(halBusyEvent);
        halBusyEvent = -1;
    }

    wokeup = 0;

    // START A TIMER TO PROVIDE PROPER TIMEOUT
    // USE timeoutms <=0 TO CONTINUE WAITING FOR A PREVIOUSLY SCHEDULED TIMEOUT
    // IN CASE OTHER EVENT WAKES UP THE CPU
    if(timeoutms > 0) {
        halFlags &= ~HAL_TIMEOUT;
        halTimeoutEvent = tmr_eventcreate(&timeouthandler, timeoutms, 0);
    }

    do {

        keymsg = keyb_getmsg();

        if(!keymsg) {
            // FIRST: ENTER LOW SPEED MODE
            if(rplTestSystemFlag(FL_QUICKRESPONSE))
                halFlags |= HAL_QUICKRESPONSE;
            else
                halFlags &= ~HAL_QUICKRESPONSE;

            if(halFlags & HAL_FASTMODE) {
                halCPUSlowMode();
                halFlags &= ~HAL_FASTMODE;
            }
            if(halFlags & HAL_HOURGLASS) {
                halSetNotification(N_HOURGLASS, ggl_color(PAL_GRAY0));
                halFlags &= ~HAL_HOURGLASS;
                halScreenUpdated();

            }

            if(wokeup) {
                if(halFlags & HAL_TIMEOUT) {
                    halFlags &= ~HAL_TIMEOUT;
                    return -1;
                }
                return 0;       // ALLOW SCREEN REFRESH REQUESTED BY OTHER IRQ'S
            }

            // LAST: GO INTO "WAIT FOR INTERRUPT"
            cpu_waitforinterrupt();
            wokeup = 1;
        }
    }
    while(!keymsg);

    return keymsg;

}

// SYSTEM CONTEXT VARIABLE
// STORES THE CONTEXT ID
// ID=0 MEANS ANY CONTEXT
// ID BIT 0 --> SET TO 1 WHEN THE COMMAND LINE IS ACTIVE OR TEXT IS BEING EDITED
// ID BITS 1 AND BIT 2 ARE RESERVED FOR FUTURE USE AND SHOULD BE ZERO
// ID=8 IS THE STACK
// ID=16 IS PICT
// ID N*8 WITH N<100 ARE RESERVED FOR THE SYSTEM APPLICATIONS (SOLVER, ETC)
// ID= N*8 --> USER CONTEXTS FROM N=100 AND UP TO 16250 ARE FREE TO USE

// SET THE KEYBOARD CONTEXT
void halSetContext(int32_t KeyContext)
{
    halScreen.KeyContext = KeyContext;
}

// AND RETRIEVE
int32_t halGetContext()
{
    return halScreen.KeyContext;
}

// TOGGLES BETWEEN ALPHA AND ANOTHER MODE
// isalpha TELLS IF ALPHA MODE IS ACTIVE, TO
// KEEP THE CURSOR IN SYNC
void halSwapCmdLineMode(int32_t isalpha)
{
    int tmp = halScreen.CursorState;

    if(((tmp & 0xff) == 'L') || ((tmp & 0xff) == 'C')) {
        // DO NOTHING IF WE ALREADY ARE IN ALPHA MODE
        if(isalpha)
            return;
    }
    else {
        if(!isalpha)
            return;
        // LOCK CAPS MODE WHEN ENTERING ALPHA MODE
        tmp &= 0x00ffffff;
        tmp |= 'C' << 24;
    }
    halScreen.CursorState &= 0x00ffff00;
    halScreen.CursorState |= tmp << 24;
    halScreen.CursorState |= (tmp >> 24) & 0xff;
}

void halSetCmdLineMode(BYTE mode)
{
    halScreen.CursorState = (halScreen.CursorState & ~0xff) | mode;
}

BYTE halGetCmdLineMode()
{
    return halScreen.CursorState & 0xff;
}

void halForceAlphaModeOn()
{
    halSwapCmdLineMode(1);
    keyb_setshiftplane(0, 0, 1, 1);
}

void halForceAlphaModeOff()
{
    halSwapCmdLineMode(0);
    keyb_setshiftplane(0, 0, 0, 0);
}

// DEBUG: DO-NOTHING KEYBOARD HANDLER
void dummyKeyhandler(WORD keymsg)
{
    UNUSED(keymsg);
    return;
}

// END THE CURRENTLY OPEN COMMAND LINE, RETURN 1 IF COMPILED SUCCESSFULLY
// 0 IF ERROR.
// WHEN 1, THE STACK CONTAINS THE OBJECT/S COMPILED
// WHEN 0, THE COMMAND LINE IS STILL OPEN, WITH THE ERROR HIGHLIGHTED
int32_t endCmdLineAndCompile()
{
    word_p text = uiGetCmdLineText();
    if(!text) {
        throw_dbgexception("No memory for command line",
                EX_CONT | EX_WARM | EX_RESET);
        return 0;
    }
    int32_t len = rplStrSize(text);
    word_p newobject;
    if(len) {
        newobject = rplCompile((byte_p) (text + 1), len, 1);
        if(Exceptions || (!newobject)) {
            // HIGHLIGHT THE WORD THAT CAUSED THE ERROR

            char *mainbuffer = (char *)(CmdLineText + 1);

            // COUNT LINES UNTIL THE TOKEN ERROR

            char *position = (char *)TokenStart;
            char *linestart = NULL;

            // COMPUTE LINE NUMBER
            int linenum = 1;

            while(position > mainbuffer) {
                --position;
                if(*position == '\n') {
                    ++linenum;
                    if(!linestart)
                        linestart = position + 1;
                }
            }

            // COUNT CHARACTERS FROM START OF LINE
            position = (char *)TokenStart;
            if(!linestart)
                linestart = mainbuffer;

            while(*linestart == '\r')
                ++linestart;

            int posnum = utf8nlen(linestart, position) + 1;

            // HERE linenum HAS THE LINE NUMBER OF THE TOKEN CAUSING THE ERROR
            // AND posnum IS THE POSITION OF THE FIRST CHARACTER OF THE TOKEN

            WORD SavedExceptions = Exceptions;

            Exceptions = 0;

            uiSetCurrentLine(linenum);
            uiCursorStartOfLine();
            uiCursorRight(posnum - 1);

            if(!Exceptions)
                Exceptions = SavedExceptions;

            WORD fakeprogram = 0;
            ExceptionPointer = &fakeprogram;
            halShowErrorMsg();
            Exceptions = 0;

            return 0;
        }
        else {
            // END ALPHA MODE
            halSwapCmdLineMode(0);
            keyb_setshiftplane(0, 0, 0, 0);
            if(uiGetCmdLineState() & CMDSTATE_OVERWRITE) {
                if(rplDepthData() >= 1)
                    rplDropData(1);
            }
            uiCloseCmdLine();
            halSetCmdLineHeight(0);
            halSetContext(halGetContext() & (~CONTEXT_INEDITOR));
            // RUN THE OBJECT

            rplSetEntryPoint(newobject);

            // RUN AND CLEANUP PROPERLY
            int32_t rstksave = RSTop - RStk, lamsave = LAMTop - LAMs, nlambase =
                    nLAMBase - LAMs;
            int32_t result = rplRun();

            switch (result) {
            case CLEAN_RUN:
            {
                // SOMEBODY CALLED EXITRPL EXPLICITLY
                // EVERYTHING WAS COMPLETELY CLEANED UP AND RESET
                halFlags &= ~(HAL_HALTED | HAL_AUTORESUME | HAL_FASTAUTORESUME);
                break;
            }
            case NEEDS_CLEANUP:
            {
                // UNTRAPPED ERROR
                // CLEANUP ANY GARBAGE AFTER OUR SAVED POINTER
                if(RSTop >= RStk + rstksave) {
                    RSTop = RStk + rstksave;
                }
                else {
                    rplCleanup();
                    halFlags &=
                            ~(HAL_HALTED | HAL_AUTORESUME | HAL_FASTAUTORESUME);
                }
                if(LAMTop > LAMs + lamsave)
                    LAMTop = LAMs + lamsave;
                if(nLAMBase > LAMs + nlambase)
                    nLAMBase = LAMs + nlambase;
                break;
            }

            case CODE_HALTED:
            {
                // UNTRAPPED ERROR
                // CLEANUP ANY GARBAGE AFTER OUR SAVED POINTER
                if(RSTop > RStk + rstksave) {
                    // THE CODE HALTED SOMEWHERE INSIDE!
                    halFlags |= HAL_HALTED;
                    if(Exceptions & EX_POWEROFF)
                        halFlags |= HAL_POWEROFF | HAL_FASTAUTORESUME;
                    if(Exceptions & EX_HALRESET)
                        halFlags |= HAL_RESET;
                    if(Exceptions & EX_HWRESET)
                        halFlags |= HAL_HWRESET;
                    if(Exceptions & EX_AUTORESUME) {
                        halFlags |= HAL_AUTORESUME;
                        Exceptions = 0;
                    }
                }
                else {
                    if(RSTop < RStk + rstksave) {

                        // THIS CAN ONLY HAPPEN IF SOMEHOW
                        // THE CODE ESCAPED FROM THE SECONDARY
                        // WE CREATED, THIS CAN HAPPEN WHEN USING 'CONT'
                        // INSIDE A SECONDARY AND IT'S NOT NECESSARILY BAD
                        if(CurOpcode == CMD_ENDOFCODE) {
                            rplClearErrors();
                            rplCleanup();
                        }
                        if(HaltedIPtr) {
                            halFlags |= HAL_HALTED;
                            if(Exceptions & EX_POWEROFF)
                                halFlags |= HAL_POWEROFF | HAL_FASTAUTORESUME;
                            if(Exceptions & EX_HALRESET)
                                halFlags |= HAL_RESET;
                            if(Exceptions & EX_HWRESET)
                                halFlags |= HAL_HWRESET;
                            if(Exceptions & EX_AUTORESUME) {
                                halFlags |= HAL_AUTORESUME;
                                Exceptions = 0;
                            }
                        }
                        else {
                            halFlags &=
                                    ~(HAL_HALTED | HAL_AUTORESUME |
                                    HAL_FASTAUTORESUME);
                            if(Exceptions & EX_POWEROFF)
                                halFlags |= HAL_POWEROFF | HAL_FASTAUTORESUME;
                            if(Exceptions & EX_HALRESET)
                                halFlags |= HAL_RESET;
                            if(Exceptions & EX_HWRESET)
                                halFlags |= HAL_HWRESET;
                        }

                    }
                    else {
                        // RETURN STACK WAS INTACT, RESTORE THE REST
                        if(LAMTop > LAMs + lamsave)
                            LAMTop = LAMs + lamsave;
                        if(nLAMBase > LAMs + nlambase)
                            nLAMBase = LAMs + nlambase;

                        // DON'T ALTER THE INSTRUCTION POINTER OF THE HALTED PROGRAM
                        if(CurOpcode == CMD_ENDOFCODE)
                            rplClearErrors();
                        if(HaltedIPtr) {
                            halFlags |= HAL_HALTED;
                            if(Exceptions & EX_POWEROFF)
                                halFlags |= HAL_POWEROFF | HAL_FASTAUTORESUME;
                            if(Exceptions & EX_HALRESET)
                                halFlags |= HAL_RESET;
                            if(Exceptions & EX_HWRESET)
                                halFlags |= HAL_HWRESET;

                            if(Exceptions & EX_AUTORESUME) {
                                halFlags |= HAL_AUTORESUME;
                                Exceptions = 0;
                            }
                        }
                        else {
                            halFlags &=
                                    ~(HAL_HALTED | HAL_AUTORESUME |
                                    HAL_FASTAUTORESUME);
                            if(Exceptions & EX_POWEROFF)
                                halFlags |= HAL_POWEROFF | HAL_FASTAUTORESUME;
                            if(Exceptions & EX_HALRESET)
                                halFlags |= HAL_RESET;
                            if(Exceptions & EX_HWRESET)
                                halFlags |= HAL_HWRESET;
                        }

                    }
                }

                break;
            }

            }

            if(Exceptions) {
                // TODO: SHOW ERROR MESSAGE
                if(!(halFlags & (HAL_RESET | HAL_HWRESET)))
                    halShowErrorMsg();
                Exceptions = 0;
                return 1;
            }
            // EVERYTHING WENT FINE, CLOSE THE COMMAND LINE
            return 1;

        }
    }
    else {
        // EMPTY COMMAND LINE!

        // END ALPHA MODE
        halSwapCmdLineMode(0);
        keyb_setshiftplane(0, 0, 0, 0);

        // AND COMMAND LINE
        uiCloseCmdLine();
        halSetCmdLineHeight(0);
        halSetContext(halGetContext() & (~CONTEXT_INEDITOR));

        return 1;
    }

    return 0;
}

void endCmdLine()
{
    // END ALPHA MODE
    halSwapCmdLineMode(0);
    keyb_setshiftplane(0, 0, 0, 0);

    // CLOSE COMMAND LINE DISCARDING CONTENTS
    uiCloseCmdLine();
    halSetCmdLineHeight(0);
    halSetContext(halGetContext() & (~CONTEXT_INEDITOR));
}

// **************************************************************************
// *******************    DEFAULT KEY HANDLERS     **************************
// **************************************************************************

void numberKeyHandler(WORD keymsg)
{
    if(!(halGetContext() & CONTEXT_INEDITOR)) {
        if(halGetContext() >> 5)
            return;     // DO NOTHING INSIDE A FORM, UNLESS THE EDITOR IS OPEN
        if(halGetContext() & CONTEXT_INTSTACK)
            return;     // DO NOTHING

        halSetCmdLineHeight(CMDLINE_HEIGHT);
        halSetContext(halGetContext() | CONTEXT_INEDITOR);
        if(KM_SHIFTPLANE(keymsg) & SHIFT_ALPHA)
            uiOpenCmdLine('X');
        else
            uiOpenCmdLine('D');
    }
    int32_t number;
    switch (KM_KEY(keymsg)) {
    case KB_1:
        number = '1';
        break;
    case KB_2:
        number = '2';
        break;
    case KB_3:
        number = '3';
        break;
    case KB_4:
        number = '4';
        break;
    case KB_5:
        number = '5';
        break;
    case KB_6:
        number = '6';
        break;
    case KB_7:
        number = '7';
        break;
    case KB_8:
        number = '8';
        break;
    case KB_9:
        number = '9';
        break;
    case KB_0:
        number = '0';
        break;
    }
    uiInsertCharactersN((byte_p) & number, ((byte_p) & number) + 1);
    uiAutocompleteUpdate();

}

void uiCmdRun(WORD Opcode)
{
    word_p obj = rplAllocTempObLowMem(2);
    if(obj) {

        // ENABLE UNDO
        // PRESERVE obj IN CASE OF GC
        ScratchPointer1 = obj;

        rplRemoveSnapshot(halScreen.StkUndolevels + 1);
        rplRemoveSnapshot(halScreen.StkUndolevels);
        if(halScreen.StkCurrentLevel != 1)
            rplTakeSnapshot();
        halScreen.StkCurrentLevel = 0;
        obj = ScratchPointer1;

        obj[0] = Opcode;
        obj[1] = CMD_ENDOFCODE;
        obj[2] = CMD_QSEMI;     // THIS IS FOR SAFETY REASONS
        rplSetEntryPoint(obj);
        int32_t iseval = (Opcode == (CMD_OVR_XEQ)) || (Opcode == (CMD_OVR_EVAL))
                || (Opcode == (CMD_OVR_EVAL1));

        if(iseval) {
            // STORE THE OBJECT/OPCODE THAT MAY CAUSE AN EXCEPTION
            if(rplDepthData() > 0)
                BlameCmd = rplPeekData(1);
            else
                BlameCmd = 0;
        }
        else
            BlameCmd = 0;
        int32_t rstksave = RSTop - RStk, lamsave = LAMTop - LAMs, nlambase =
                nLAMBase - LAMs;
        int32_t result = rplRun();
        switch (result) {

        case CLEAN_RUN:
        {
            // SOMEBODY CALLED EXITRPL EXPLICITLY
            // EVERYTHING WAS COMPLETELY CLEANED UP AND RESET
            halFlags &= ~(HAL_HALTED | HAL_AUTORESUME | HAL_FASTAUTORESUME);
            break;
        }
        case NEEDS_CLEANUP:
        {
            // UNTRAPPED ERROR
            // CLEANUP ANY GARBAGE AFTER OUR SAVED POINTER
            if(RSTop >= RStk + rstksave) {
                RSTop = RStk + rstksave;
                // BLAME THE ERROR ON THE COMMAND WE CALLED
                if(!rplIsTempObPointer(ExceptionPointer)) {
                    if(BlameCmd != 0)
                        rplBlameError(BlameCmd);
                }
            }
            else {
                rplCleanup();
                halFlags &= ~(HAL_HALTED | HAL_AUTORESUME | HAL_FASTAUTORESUME);
            }
            if(LAMTop > LAMs + lamsave)
                LAMTop = LAMs + lamsave;
            if(nLAMBase > LAMs + nlambase)
                nLAMBase = LAMs + nlambase;
            break;
        }

        case CODE_HALTED:
        {
            // UNTRAPPED ERROR
            // CLEANUP ANY GARBAGE AFTER OUR SAVED POINTER
            if(RSTop > RStk + rstksave) {
                // THE CODE HALTED SOMEWHERE INSIDE!
                halFlags |= HAL_HALTED;
                if(Exceptions & EX_POWEROFF)
                    halFlags |= HAL_POWEROFF | HAL_FASTAUTORESUME;
                if(Exceptions & EX_HALRESET)
                    halFlags |= HAL_RESET;
                if(Exceptions & EX_HWRESET)
                    halFlags |= HAL_HWRESET;

                if(Exceptions & EX_AUTORESUME) {
                    halFlags |= HAL_AUTORESUME;
                    Exceptions = 0;
                }
            }
            else {
                if(RSTop < RStk + rstksave) {
                    // THIS CAN ONLY HAPPEN IF SOMEHOW
                    // THE CODE ESCAPED FROM THE SECONDARY
                    // WE CREATED, THIS CAN HAPPEN WHEN USING 'CONT'
                    // INSIDE A SECONDARY AND IT'S NOT NECESSARILY BAD
                    if(CurOpcode == CMD_ENDOFCODE) {
                        rplClearErrors();
                        rplCleanup();
                    }
                    if(HaltedIPtr) {
                        halFlags |= HAL_HALTED;
                        if(Exceptions & EX_POWEROFF)
                            halFlags |= HAL_POWEROFF | HAL_FASTAUTORESUME;
                        if(Exceptions & EX_HALRESET)
                            halFlags |= HAL_RESET;
                        if(Exceptions & EX_HWRESET)
                            halFlags |= HAL_HWRESET;

                        if(Exceptions & EX_AUTORESUME) {
                            halFlags |= HAL_AUTORESUME;
                            Exceptions = 0;
                        }
                    }
                    else {
                        halFlags &=
                                ~(HAL_HALTED | HAL_AUTORESUME |
                                HAL_FASTAUTORESUME);
                        if(Exceptions & EX_POWEROFF)
                            halFlags |= HAL_POWEROFF | HAL_FASTAUTORESUME;
                        if(Exceptions & EX_HALRESET)
                            halFlags |= HAL_RESET;
                        if(Exceptions & EX_HWRESET)
                            halFlags |= HAL_HWRESET;
                    }

                }
                else {
                    // RETURN STACK WAS INTACT, IT HALTED AT OUR OWN SECONDARY
                    //if(LAMTop>LAMs+lamsave) LAMTop=LAMs+lamsave;
                    //if(nLAMBase>LAMs+nlambase) nLAMBase=LAMs+nlambase;

                    // DON'T ALTER THE INSTRUCTION POINTER OF THE HALTED PROGRAM
                    if(HaltedIPtr) {
                        halFlags |= HAL_HALTED;
                        if(Exceptions & EX_POWEROFF)
                            halFlags |= HAL_POWEROFF | HAL_FASTAUTORESUME;
                        if(Exceptions & EX_HALRESET)
                            halFlags |= HAL_RESET;
                        if(Exceptions & EX_HWRESET)
                            halFlags |= HAL_HWRESET;

                        if(Exceptions & EX_AUTORESUME) {
                            halFlags |= HAL_AUTORESUME;
                            Exceptions = 0;
                        }
                    }
                    else {
                        halFlags &=
                                ~(HAL_HALTED | HAL_AUTORESUME |
                                HAL_FASTAUTORESUME);
                        if(Exceptions & EX_POWEROFF)
                            halFlags |= HAL_POWEROFF | HAL_FASTAUTORESUME;
                        if(Exceptions & EX_HALRESET)
                            halFlags |= HAL_RESET;
                        if(Exceptions & EX_HWRESET)
                            halFlags |= HAL_HWRESET;
                    }
                    rplClearErrors();

                }
            }

            break;
        }
        }

    }

}

void uiCmdRunHide(WORD Opcode, int32_t narguments)
{
    word_p obj = rplAllocTempObLowMem(2);
    if(obj) {

        ScratchPointer1 = obj;
        // ENABLE UNDO
        rplRemoveSnapshot(halScreen.StkUndolevels + 1);
        rplRemoveSnapshot(halScreen.StkUndolevels);
        if(halScreen.StkCurrentLevel != 1)
            rplTakeSnapshotHide(narguments);
        halScreen.StkCurrentLevel = 0;
        obj = ScratchPointer1;

        obj[0] = Opcode;
        obj[1] = CMD_ENDOFCODE;
        obj[2] = CMD_QSEMI;     // THIS IS FOR SAFETY REASONS
        rplSetEntryPoint(obj);
        int32_t iseval = (Opcode == (CMD_OVR_XEQ)) || (Opcode == (CMD_OVR_EVAL))
                || (Opcode == (CMD_OVR_EVAL1));

        if(iseval) {
            // STORE THE OBJECT/OPCODE THAT MAY CAUSE AN EXCEPTION
            if(rplDepthData() > 0)
                BlameCmd = rplPeekData(1);
            else
                BlameCmd = 0;
        }
        else
            BlameCmd = 0;
        int32_t rstksave = RSTop - RStk, lamsave = LAMTop - LAMs, nlambase =
                nLAMBase - LAMs;
        int32_t result = rplRun();
        switch (result) {

        case CLEAN_RUN:
        {
            // SOMEBODY CALLED EXITRPL EXPLICITLY
            // EVERYTHING WAS COMPLETELY CLEANED UP AND RESET
            halFlags &= ~(HAL_HALTED | HAL_AUTORESUME | HAL_FASTAUTORESUME);
            break;
        }
        case NEEDS_CLEANUP:
        {
            // UNTRAPPED ERROR
            // CLEANUP ANY GARBAGE AFTER OUR SAVED POINTER
            if(RSTop >= RStk + rstksave) {
                RSTop = RStk + rstksave;
                // BLAME THE ERROR ON THE COMMAND WE CALLED
                if(BlameCmd != 0)
                    rplBlameError(BlameCmd);
            }
            else {
                rplCleanup();
                halFlags &= ~(HAL_HALTED | HAL_AUTORESUME | HAL_FASTAUTORESUME);
            }
            if(LAMTop > LAMs + lamsave)
                LAMTop = LAMs + lamsave;
            if(nLAMBase > LAMs + nlambase)
                nLAMBase = LAMs + nlambase;
            break;
        }

        case CODE_HALTED:
        {
            // UNTRAPPED ERROR
            // CLEANUP ANY GARBAGE AFTER OUR SAVED POINTER
            if(RSTop > RStk + rstksave) {
                // THE CODE HALTED SOMEWHERE INSIDE!
                halFlags |= HAL_HALTED;
                if(Exceptions & EX_POWEROFF)
                    halFlags |= HAL_POWEROFF | HAL_FASTAUTORESUME;
                if(Exceptions & EX_HALRESET)
                    halFlags |= HAL_RESET;
                if(Exceptions & EX_HWRESET)
                    halFlags |= HAL_HWRESET;

                if(Exceptions & EX_AUTORESUME) {
                    halFlags |= HAL_AUTORESUME;
                    Exceptions = 0;
                }

            }
            else {
                if(RSTop < RStk + rstksave) {
                    // THIS CAN ONLY HAPPEN IF SOMEHOW
                    // THE CODE ESCAPED FROM THE SECONDARY
                    // WE CREATED, THIS CAN HAPPEN WHEN USING 'CONT'
                    // INSIDE A SECONDARY AND IT'S NOT NECESSARILY BAD
                    if(CurOpcode == CMD_ENDOFCODE) {
                        rplClearErrors();
                        rplCleanup();
                    }
                    if(HaltedIPtr) {
                        halFlags |= HAL_HALTED;
                        if(Exceptions & EX_POWEROFF)
                            halFlags |= HAL_POWEROFF | HAL_FASTAUTORESUME;
                        if(Exceptions & EX_HALRESET)
                            halFlags |= HAL_RESET;
                        if(Exceptions & EX_HWRESET)
                            halFlags |= HAL_HWRESET;

                        if(Exceptions & EX_AUTORESUME) {
                            halFlags |= HAL_AUTORESUME;
                            Exceptions = 0;
                        }
                    }
                    else {
                        halFlags &=
                                ~(HAL_HALTED | HAL_AUTORESUME |
                                HAL_FASTAUTORESUME);
                        if(Exceptions & EX_POWEROFF)
                            halFlags |= HAL_POWEROFF | HAL_FASTAUTORESUME;
                        if(Exceptions & EX_HALRESET)
                            halFlags |= HAL_RESET;
                        if(Exceptions & EX_HWRESET)
                            halFlags |= HAL_HWRESET;
                    }

                }
                else {
                    // RETURN STACK WAS INTACT, RESTORE THE REST
                    //if(LAMTop>LAMs+lamsave) LAMTop=LAMs+lamsave;
                    //if(nLAMBase>LAMs+nlambase) nLAMBase=LAMs+nlambase;

                    // DON'T ALTER THE INSTRUCTION POINTER OF THE HALTED PROGRAM
                    if(HaltedIPtr) {
                        halFlags |= HAL_HALTED;
                        if(Exceptions & EX_POWEROFF)
                            halFlags |= HAL_POWEROFF | HAL_FASTAUTORESUME;
                        if(Exceptions & EX_HALRESET)
                            halFlags |= HAL_RESET;
                        if(Exceptions & EX_HWRESET)
                            halFlags |= HAL_HWRESET;

                        if(Exceptions & EX_AUTORESUME) {
                            halFlags |= HAL_AUTORESUME;
                            Exceptions = 0;
                        }
                    }
                    else {
                        halFlags &=
                                ~(HAL_HALTED | HAL_AUTORESUME |
                                HAL_FASTAUTORESUME);
                        if(Exceptions & EX_POWEROFF)
                            halFlags |= HAL_POWEROFF | HAL_FASTAUTORESUME;
                        if(Exceptions & EX_HALRESET)
                            halFlags |= HAL_RESET;
                        if(Exceptions & EX_HWRESET)
                            halFlags |= HAL_HWRESET;
                    }
                    rplClearErrors();

                }
            }

            break;
        }
        }

    }

}

// EXECUTE THE OPCODE IN A PROTECTED "TRANSPARENT" ENVIRONMENT
// THE USER STACK. RETURN STACK AND LAM ENVIRONMENTS ARE
// ALL PRESERVED AND PROTECTED
// THE COMMAND RECEIVES nargs IN THE STACK AND RETURNS AT MOST nresults
// IT RETURNS THE NUMBER OF RESULTS LEFT IN THE STACK

int32_t uiCmdRunTransparent(WORD Opcode, int32_t nargs, int32_t nresults)
{
    word_p obj = rplAllocTempObLowMem(2);
    if(obj) {
        obj[0] = Opcode;
        obj[1] = CMD_ENDOFCODE;
        obj[2] = CMD_QSEMI;     // THIS IS FOR SAFETY REASONS

        int32_t rsave, lamsave, nlambase, retvalue;
        WORD exceptsave, errcodesave;
        // PRESERVE VARIOUS STACK POINTERS

        exceptsave = Exceptions;
        errcodesave = ErrorCode;

        rplSetExceptionHandler(0);      // SAVE CURRENT EXCEPTION HANDLERS
        rplPushRet(IPtr);       // SAVE THE CURRENT INSTRUCTION POINTER

        ScratchPointer1 = obj;
        rplTakeSnapshotN(nargs);        // RUN THE COMMAND WITH A PROTECTED STACK WITH nargs ARGUMENTS ONLY
        obj = ScratchPointer1;
        rsave = RSTop - RStk;   // PROTECT THE RETURN STACK
        lamsave = LAMTop - LAMs;        // PROTECT LAM ENVIRONMENTS
        nlambase = nLAMBase - LAMs;

        Exceptions = 0; // REMOVE ALL EXCEPTIONS

        rplSetEntryPoint(obj);

        rplRun();

        // DISCARD ANY ERRORS DURING EXECUTION,  IDEALLY IT HIT THE BREAKPOINT
        if(Exceptions != EX_HALT) {
            // THERE WAS SOME OTHER ERROR DURING EXECUTION, DISCARD ALL OUTPUT FROM THE FAILED PROGRAM
            rplClearData();
        }

        Exceptions = 0;

        // MANUAL RESTORE

        if(RSTop >= RStk + rsave)
            RSTop = RStk + rsave;       // IF RSTop<RStk+rsave THE RETURN STACK WAS COMPLETELY CORRUPTED, SHOULD NEVER HAPPEN BUT...
        else
            rplCleanup();
        if(LAMTop >= LAMs + lamsave)
            LAMTop = LAMs + lamsave;    // OTHERWISE THE LAM ENVIRONMENTS WERE DESTROYED, SHOULD NEVER HAPPEN BUT...
        else
            rplCleanup();
        if(nLAMBase >= LAMs + nlambase)
            nLAMBase = LAMs + nlambase; // OTHERWISE THE LAM ENVIRONMENTS WERE DESTROYED, SHOULD NEVER HAPPEN BUT...
        else
            rplCleanup();

        // CLEAN THE STACK
        if(rplDepthData() > nresults) {
            int32_t f;
            int32_t depth = rplDepthData(), offset = depth - nresults;
            for(f = depth; f > depth - nresults; --f) {
                rplOverwriteData(f, rplPeekData(f - offset));
            }
            rplDropData(offset);
        }
        // HERE THE STACK CONTAINS UP TO nresults ONLY

        rplTakeSnapshotAndClear();      // HERE SNAPSHOT1 = RESULTS, SNAPSHOT2 = PREVIOUS STACK
        rplRevertToSnapshot(2); // RECOVER THE PREVIOUS STACK
        rplDropData(nargs);     // REMOVE THE ORIGINAL ARGUMENTS
        nresults = retvalue = rplDepthSnapshot(1);      // GET THE NUMBER OF RESULTS
        while(nresults) {
            rplPushData(rplPeekSnapshot(1, nresults));  // EXTRACT THE RESULTS INTO THE CURRENT STACK
            --nresults;
        }
        rplRemoveSnapshot(1);   // AND CLEANUP

        // RESTORE THE ERROR CODES FIRST, TO CAPTURE ANY ERRORS DURING POPPING THE RETURN STACK
        Exceptions = exceptsave;
        ErrorCode = errcodesave;

        // RESTORE THE IP POINTER
        IPtr = rplPopRet();

        // AND THE ERROR HANDLERS
        rplRemoveExceptionHandler();

        // IF EVERYTHING WENT WELL, HERE WE HAVE THE SAME ENVIRONMENT AS BEFORE
        // IF SOMETHING GOT CORRUPTED, WE SHOULD HAVE AN INTERNAL EMPTY RSTACK ERROR
        return retvalue;

    }
    return 0;
}

// RESTURE THE STACK TO WHAT IT WAS AT THE GIVEN LEVEL
// LEVEL 1 = MOST IMMEDIATE ... LEVEL StkUndoLevel = OLDEST
// SPECIAL CASE: LEVEL 0 = USER'S CURRENT STACK
int32_t uiRestoreUndoLevel(int32_t level)
{
    int32_t nlevels = rplCountSnapshots();

    if(level < 1)
        return halScreen.StkCurrentLevel;
    if(level > nlevels)
        level = nlevels;

    if(!halScreen.StkCurrentLevel) {
        // WHEN CURRENT LEVEL IS ZERO, MEANS THE PREVIOUS ACTION WAS NOT A RESTORE
        // WE NEED TO PRESERVE THE CURRENT STACK AS LEVEL 0
        rplTakeSnapshot();
        ++level;
    }

    // HERE LEVEL 1 = USER STACK, 2..(N+1) = N UNDO LEVELS PRESERVED

    rplRestoreSnapshot(level);
    return level;

}

void uiStackUndo()
{
    halScreen.StkCurrentLevel =
            uiRestoreUndoLevel(halScreen.StkCurrentLevel + 1);
}

void uiStackRedo()
{
    halScreen.StkCurrentLevel =
            uiRestoreUndoLevel(halScreen.StkCurrentLevel - 1);
}

// TYPICAL COMMAND KEY HANDLER.
// EXECUTES Opcode IN DIRECT MODE
// INSERTS Progmode AS TEXT IN THE COMMAND LINE WHEN IN PROGRAMMING MODE
// IF IsFunc == 0 --> IN ALG MODE INSERT THE SAME TEXT AS IN PROG. MODE
//    IsFunc == 1 --> IN ALG MODE INSERT THE SAME TEXT AS IN PROG, WITH FUNCTION PARENTHESIS
//    IsFunc == 2 --> IN ALG MODE, RUN THE OPCODE DIRECTLY, AS IN 'D' MODE
//    IsFunc < 0  --> NOT ALLOWED IN SYMBOLIC (ALG) MODE, DO NOTHING

void cmdKeyHandler(WORD Opcode, byte_p Progmode, int32_t IsFunc)
{
    if(!(halGetContext() & CONTEXT_INEDITOR)) {
        if(halGetContext() & CONTEXT_STACK) {
            // ACTION WHEN IN THE STACK
            uiCmdRun(Opcode);
            if(Exceptions) {
                // TODO: SHOW ERROR MESSAGE
                halShowErrorMsg();
                Exceptions = 0;
            }
            else
                halScreen.DirtyFlag |= MENU1_DIRTY | MENU2_DIRTY | STAREA_DIRTY;
            halScreen.DirtyFlag |= STACK_DIRTY;

        }

    }
    else {
        // ACTION INSIDE THE EDITOR
        switch (halScreen.CursorState & 0xff) {

        case 'D':      // DIRECT EXECUTION
        {

            if(endCmdLineAndCompile()) {
                uiCmdRun(Opcode);
                if(Exceptions) {
                    // TODO: SHOW ERROR MESSAGE
                    halShowErrorMsg();
                    Exceptions = 0;
                }
                else {
                    if(rplTestSystemFlag(FL_LASTMENU))
                        halScreen.DirtyFlag |=
                                MENU1_DIRTY | MENU2_DIRTY | STAREA_DIRTY;
                }
                halScreen.DirtyFlag |= STACK_DIRTY;
            }
            break;
        }
        case 'P':      // PROGRAMMING MODE
            // TODO: SEPARATE TOKENS
            uiSeparateToken();
            uiInsertCharacters(Progmode);
            uiSeparateToken();
            uiAutocompleteUpdate();
            break;

        case 'L':
        case 'C':
        case 'A':      // ALPHANUMERIC MODE
            if(IsFunc >= 0) {

                if(IsFunc == 2) {
                    if(endCmdLineAndCompile()) {
                        uiCmdRun(Opcode);
                        if(Exceptions) {
                            // TODO: SHOW ERROR MESSAGE
                            halShowErrorMsg();
                            Exceptions = 0;
                        }
                        else
                            halScreen.DirtyFlag |=
                                    MENU1_DIRTY | MENU2_DIRTY | STAREA_DIRTY;
                        halScreen.DirtyFlag |= STACK_DIRTY;
                    }
                    break;
                }
                if(IsFunc < 2) {
                    uiInsertCharacters(Progmode);
                    if(IsFunc == 1) {
                        uiInsertCharacters((byte_p) "()");
                        uiCursorLeft(1);
                    }
                    uiAutocompleteUpdate();
                }
            }
            break;
        default:
            break;
        }
    }
}

void transpcmdKeyHandler(WORD Opcode)
{
    if(!(halGetContext() & CONTEXT_INEDITOR)) {
        if(halGetContext() & CONTEXT_STACK) {
            // ACTION WHEN IN THE STACK
            uiCmdRun(Opcode);
            if(Exceptions) {
                // TODO: SHOW ERROR MESSAGE
                halShowErrorMsg();
                Exceptions = 0;
            }
            else
                halScreen.DirtyFlag |= MENU1_DIRTY | MENU2_DIRTY | STAREA_DIRTY;
            halScreen.DirtyFlag |= STACK_DIRTY;
        }

    }
    else {
        // ACTION INSIDE THE EDITOR
        uiCmdRun(Opcode);
        if(Exceptions) {
            // TODO: SHOW ERROR MESSAGE
            halShowErrorMsg();
            Exceptions = 0;
        }
        else
            halScreen.DirtyFlag |= MENU1_DIRTY | MENU2_DIRTY | STAREA_DIRTY;
        halScreen.DirtyFlag |= STACK_DIRTY;
    }

}

void varsKeyHandler(WORD keymsg, int32_t menunum, int32_t varnum)
{
#ifdef TARGET_PRIME1
    // SWITCH MENUS
    if(halKeyMenuSwitch)
        menunum = (menunum==2) ? 1 : 2;
#endif /* TARGET_PRIME1 */
    if(KM_MESSAGE(keymsg) == KM_LPRESS) {
        // ENTER MENU HELP MODE
        // KILL ANY PENDING POPUPS
        halScreen.HelpMode = (menunum << 16) | varnum;
        halScreen.DirtyFlag |= MENU1_DIRTY | MENU2_DIRTY;
        return;
    }

    if(KM_MESSAGE(keymsg) == KM_KEYUP) {
        if(halScreen.HelpMode) {
            halCancelPopup();
            halScreen.HelpMode = 0;
            halScreen.DirtyFlag |= MENU1_DIRTY | MENU2_DIRTY | STAREA_DIRTY;
        }

        return;
    }

    // DEFAULT PRESS MESSAGE
    if(!(halGetContext() & CONTEXT_INEDITOR)) {
        if(!(halGetContext() & (CONTEXT_INTSTACK | CONTEXT_PICT |
                        CONTEXT_PLOT))) {
            // ACTION WHEN IN THE STACK
            int64_t mcode = rplGetMenuCode(menunum);
            word_p menu = uiGetLibMenu(mcode);
            int32_t nitems = uiCountMenuItems(mcode, menu);
            int32_t idx = MENUPAGE(mcode) + varnum, page = MENUPAGE(mcode);

            rplSetLastMenu(menunum);

            if((nitems > 6) && (varnum == 5)) {
                // THIS IS THE NXT KEY
                if((KM_SHIFTPLANE(keymsg) == SHIFT_LS)
                        || (KM_SHIFTPLANE(keymsg) == SHIFT_LSHOLD))
                    page -= 5;
                else
                    page += 5;
                if(page >= nitems)
                    page = 0;
                if(page <= -5) {
                    page = nitems / 5;
                    page *= 5;
                    if(page == nitems)
                        page -= 5;
                }
                if(page < 0)
                    page = 0;
                rplSetMenuCode(menunum, SETMENUPAGE(mcode, page));
                halScreen.DirtyFlag |= MENU1_DIRTY | MENU2_DIRTY;
                return;
            }
            // THIS IS A REGULAR VAR KEY

            word_p item = uiGetMenuItem(mcode, menu, idx);

            word_p action = uiGetMenuItemAction(item, KM_SHIFTPLANE(keymsg));
            WORD Opcode = 0;
            int32_t hideargument = 1;

            if(!action)
                return;

            switch (KM_SHIFTPLANE(keymsg)) {
            case SHIFT_LS:
            case SHIFT_LSHOLD:
            {
                // DO DIFFERENT ACTIONS BASED ON OBJECT TYPE

                if(ISIDENT(*action)) {
                    // USER IS TRYING TO 'STO' INTO THE VARIABLE
                    rplPushData(action);        // PUSH THE NAME ON THE STACK
                    Opcode = CMD_STO;
                    break;
                }
                if(ISUNIT(*action)) {
                    // FOR UNITS, TRY TO CONVERT
                    rplPushData(action);        // PUSH THE NAME ON THE STACK
                    Opcode = CMD_CONVERT;
                    break;
                }

                if(ISLIBRARY(*action)) {
                    // SHOW THE LIBRARY MENU
                    int64_t libmcode =
                            (((int64_t) action[2]) << 32) | MKMENUCODE(0,
                            DOLIBPTR, 0, 0);
                    word_p numobject = rplNewint32_t(libmcode, HEXint32_t);

                    if(!numobject || Exceptions)
                        return;

                    rplPushDataNoGrow(numobject);
                    rplSaveMenuHistory(menunum);
                    rplChangeMenu(menunum, rplPopData());

                    if(menunum == 1)
                        halScreen.DirtyFlag |= MENU1_DIRTY;
                    else
                        halScreen.DirtyFlag |= MENU2_DIRTY;

                    break;
                }
                // ALL OTHER OBJECTS AND COMMANDS, DO XEQ
                rplPushData(action);
                Opcode = (CMD_OVR_XEQ);
                break;

            }
            case SHIFT_RS:
            case SHIFT_RSHOLD:
            {
                // DO DIFFERENT ACTIONS BASED ON OBJECT TYPE

                if(ISIDENT(*action)) {
                    // USER IS TRYING TO 'RCL' THE VARIABLE
                    rplPushData(action);        // PUSH THE NAME ON THE STACK
                    Opcode = CMD_RCL;
                    break;
                }
                if(ISUNIT(*action)) {
                    // FOR UNITS, APPLY THE INVERSE
                    rplPushData(action);        // PUSH THE NAME ON THE STACK
                    Opcode = (CMD_OVR_DIV);
                    break;
                }

                if(ISLIBRARY(*action)) {
                    // SHOW THE LIBRARY MENU
                    int64_t libmcode =
                            (((int64_t) action[2]) << 32) | MKMENUCODE(0,
                            DOLIBPTR, 0, 0);
                    word_p numobject = rplNewint32_t(libmcode, HEXint32_t);

                    if(!numobject || Exceptions)
                        return;

                    rplPushDataNoGrow(numobject);
                    rplSaveMenuHistory(menunum);
                    rplChangeMenu(menunum, rplPopData());

                    if(menunum == 1)
                        halScreen.DirtyFlag |= MENU1_DIRTY;
                    else
                        halScreen.DirtyFlag |= MENU2_DIRTY;

                    break;
                }
                // ALL OTHER OBJECTS AND COMMANDS, DO XEQ
                rplPushData(action);
                Opcode = (CMD_OVR_XEQ);
                break;

            }
            default:
            {
                // DO DIFFERENT ACTIONS BASED ON OBJECT TYPE

                if(ISIDENT(*action)) {
                    // JUST EVAL THE VARIABLE
                    rplPushData(action);        // PUSH THE NAME ON THE STACK
                    Opcode = (CMD_OVR_EVAL1);
                    break;
                }
                if(ISUNIT(*action)) {
                    // FOR UNITS, APPLY THE INVERSE
                    rplPushData(action);        // PUSH THE NAME ON THE STACK
                    Opcode = (CMD_OVR_MUL);
                    break;
                }
                if(ISLIBRARY(*action)) {
                    // SHOW THE LIBRARY MENU
                    int64_t libmcode =
                            (((int64_t) action[2]) << 32) | MKMENUCODE(0,
                            DOLIBPTR, 0, 0);
                    word_p numobject = rplNewint32_t(libmcode, HEXint32_t);

                    if(!numobject || Exceptions)
                        return;

                    rplPushDataNoGrow(numobject);
                    rplSaveMenuHistory(menunum);
                    rplChangeMenu(menunum, rplPopData());

                    if(menunum == 1)
                        halScreen.DirtyFlag |= MENU1_DIRTY;
                    else
                        halScreen.DirtyFlag |= MENU2_DIRTY;

                    break;
                }

                // ALL OTHER OBJECTS AND COMMANDS, DO XEQ
                rplPushData(action);
                Opcode = (CMD_OVR_XEQ);
                break;

            }
            }

            if(Opcode)
                uiCmdRunHide(Opcode, hideargument);
            if(Exceptions) {
                // TODO: SHOW ERROR MESSAGE
                halShowErrorMsg();
                Exceptions = 0;
            }
            else
                halScreen.DirtyFlag |= MENU1_DIRTY | MENU2_DIRTY;
            halScreen.DirtyFlag |= STACK_DIRTY | STAREA_DIRTY;
        }

    }
    else {
        // ACTION INSIDE THE EDITOR
        int64_t mcode = rplGetMenuCode(menunum);
        word_p menu = uiGetLibMenu(mcode);
        int32_t nitems = uiCountMenuItems(mcode, menu);
        int32_t idx = MENUPAGE(mcode) + varnum, page = MENUPAGE(mcode);

        rplSetLastMenu(menunum);

        if((nitems > 6) && (varnum == 5)) {
            // THIS IS THE NXT KEY
            if((KM_SHIFTPLANE(keymsg) == SHIFT_LS)
                    || (KM_SHIFTPLANE(keymsg) == SHIFT_LSHOLD))
                page -= 5;
            else
                page += 5;
            if(page >= nitems)
                page = 0;
            if(page <= -5) {
                page = nitems / 5;
                page *= 5;
                if(page == nitems)
                    page -= 5;
            }
            if(page < 0)
                page = 0;
            rplSetMenuCode(menunum, SETMENUPAGE(mcode, page));
            halScreen.DirtyFlag |= MENU1_DIRTY | MENU2_DIRTY;
            return;
        }
        // THIS IS A REGULAR VAR KEY

        word_p item = uiGetMenuItem(mcode, menu, idx);

        word_p action = uiGetMenuItemAction(item, KM_SHIFTPLANE(keymsg));
        WORD Opcode = 0;
        int32_t hideargument = 1;

        if(!action)
            return;

        switch (KM_SHIFTPLANE(keymsg)) {
        case SHIFT_LS:
        case SHIFT_LSHOLD:
        {
            // DO DIFFERENT ACTIONS BASED ON OBJECT TYPE

            if(ISIDENT(*action)) {
                switch (halScreen.CursorState & 0xff) {
                case 'D':
                case 'A':
                    if(endCmdLineAndCompile()) {
                        // FIND THE VARIABLE AGAIN, IT MIGHT'VE MOVED DUE TO GC
                        menu = uiGetLibMenu(mcode);
                        item = uiGetMenuItem(mcode, menu,
                                MENUPAGE(mcode) + varnum);
                        action = uiGetMenuItemAction(item,
                                KM_SHIFTPLANE(keymsg));

                        // USER IS TRYING TO 'STO' INTO THE VARIABLE
                        rplPushData(action);    // PUSH THE NAME ON THE STACK
                        Opcode = CMD_STO;
                    }
                    break;
                case 'P':
                    // USER IS TRYING TO 'STO' INTO THE VARIABLE
                    uiSeparateToken();
                    uiInsertCharacters((byte_p) "'");
                    uiInsertCharactersN((byte_p) (action + 1),
                            (byte_p) (action + 1) + rplGetIdentLength(action));
                    uiInsertCharacters((byte_p) "' STO");
                    uiSeparateToken();
                    uiAutocompleteUpdate();
                    break;
                }
                break;
            }
            if(ISUNIT(*action)) {

                switch (halScreen.CursorState & 0xff) {
                case 'D':
                    if(endCmdLineAndCompile()) {
                        // FIND THE VARIABLE AGAIN, IT MIGHT'VE MOVED DUE TO GC
                        menu = uiGetLibMenu(mcode);
                        item = uiGetMenuItem(mcode, menu,
                                MENUPAGE(mcode) + varnum);
                        action = uiGetMenuItemAction(item,
                                KM_SHIFTPLANE(keymsg));

                        // USER IS TRYING TO CONVERT
                        rplPushData(action);    // PUSH THE NAME ON THE STACK
                        Opcode = CMD_CONVERT;
                    }
                    break;

                case 'A':
                case 'P':
                {
                    byte_p string, endstring;
                    if (!rplGetDecompiledString(action, DECOMP_EDIT | DECOMP_NOHINTS, &string, &endstring))
                        break; // ERROR WITHIN A MENU PROGRAM! JUST IGNORE FOR NOW

                    uiSeparateToken();
                    uiInsertCharactersN(string, endstring);
                    uiSeparateToken();
                    uiInsertCharacters((byte_p) "CONVERT");
                    uiSeparateToken();
                    uiAutocompleteUpdate();

                    break;
                }

                }
                break;
            }

            if(ISLIBRARY(*action)) {

                // SHOW THE LIBRARY MENU
                int64_t libmcode =
                        (((int64_t) action[2]) << 32) | MKMENUCODE(0, DOLIBPTR,
                        0, 0);
                word_p numobject = rplNewint32_t(libmcode, HEXint32_t);

                if(!numobject || Exceptions)
                    return;

                rplPushDataNoGrow(numobject);
                rplSaveMenuHistory(menunum);
                rplChangeMenu(menunum, rplPopData());

                if(menunum == 1)
                    halScreen.DirtyFlag |= MENU1_DIRTY;
                else
                    halScreen.DirtyFlag |= MENU2_DIRTY;

                break;
            }

            if(ISPROGRAM(*action)) {
                if(!ISSECO(*action)) {
                    // IT'S A DOCOL PROGRAM, EXECUTE TRANSPARENTLY
                    rplPushData(action);        // PUSH THE NAME ON THE STACK
                    Opcode = CMD_OVR_XEQ;
                    break;
                }
            }

            // ALL OTHER OBJECTS AND COMMANDS, DO XEQ AFTER ENDING THE COMMAND LINE
            if(endCmdLineAndCompile()) {
                // FIND THE VARIABLE AGAIN, IT MIGHT'VE MOVED DUE TO GC
                menu = uiGetLibMenu(mcode);
                item = uiGetMenuItem(mcode, menu, MENUPAGE(mcode) + varnum);
                action = uiGetMenuItemAction(item, KM_SHIFTPLANE(keymsg));

                // USER IS TRYING TO 'STO' INTO THE VARIABLE
                rplPushData(action);    // PUSH THE NAME ON THE STACK
                Opcode = CMD_OVR_XEQ;
                break;
            }
            break;

        }
        case SHIFT_RS:
        case SHIFT_RSHOLD:
        {
            // DO DIFFERENT ACTIONS BASED ON OBJECT TYPE

            if(ISIDENT(*action)) {
                switch (halScreen.CursorState & 0xff) {
                case 'D':

                    if(KM_SHIFTPLANE(keymsg) & SHIFT_HOLD) {
                        //  DECOMPILE THE CONTENTS AND INSERT DIRECTLY INTO THE COMMAND LINE

                        word_p *var = rplFindGlobal(action, 1);
                        byte_p string = 0, endstring;

                        if(var) {

                            if(ISDIR(*var[1])) {
                                // VARIABLE IS A DIRECTORY, DON'T RCL
                                // BUT PUT THE NAME
                                string = (byte_p) (action + 1);
                                endstring = string + rplGetIdentLength(action);
                            }
                            else {
                                rplGetDecompiledString(var[1], DECOMP_EDIT, &string, &endstring);
                            }

                            if(string) {

                                uiSeparateToken();
                                int32_t nlines =
                                        uiInsertCharactersN(string, endstring);
                                if(nlines)
                                    uiStretchCmdLine(nlines);
                                uiSeparateToken();
                                uiAutocompleteUpdate();
                            }
                        }
                        break;

                    }

                    // NOT HOLD, JUST END THE COMMAND LINE AND RCL THE VARIABLE

                    if(endCmdLineAndCompile()) {
                        // FIND THE VARIABLE AGAIN, IT MIGHT'VE MOVED DUE TO GC
                        menu = uiGetLibMenu(mcode);
                        item = uiGetMenuItem(mcode, menu,
                                MENUPAGE(mcode) + varnum);
                        action = uiGetMenuItemAction(item,
                                KM_SHIFTPLANE(keymsg));

                        // USER IS TRYING TO 'RCL' THE VARIABLE
                        rplPushData(action);    // PUSH THE NAME ON THE STACK
                        Opcode = CMD_RCL;
                    }
                    break;
                case 'A':
                    // USER IS TRYING TO 'RCL' THE VARIABLE

                    if(KM_SHIFTPLANE(keymsg) & SHIFT_HOLD) {
                        //  DECOMPILE THE CONTENTS AND INSERT DIRECTLY INTO THE COMMAND LINE

                        word_p *var = rplFindGlobal(action, 1);
                        byte_p string = 0, endstring;

                        if(var) {

                            if(ISDIR(*var[1])) {
                                // VARIABLE IS A DIRECTORY, DON'T RCL
                                // BUT PUT THE NAME
                                string = (byte_p) (action + 1);
                                endstring = string + rplGetIdentLength(action);
                            }
                            else {
                                rplGetDecompiledStringWithoutTickmarks(var[1], DECOMP_EDIT | DECOMP_NOHINTS, &string, &endstring);
                            }

                            if(string) {
                                int32_t nlines =
                                        uiInsertCharactersN(string, endstring);
                                if(nlines)
                                    uiStretchCmdLine(nlines);

                                uiAutocompleteUpdate();
                            }
                        }
                        break;

                    }

                    // JUST INSERT THE NAME IN ALGEBRAIC MODE

                    uiInsertCharactersN((byte_p) (action + 1),
                            (byte_p) (action + 1) + rplGetIdentLength(action));
                    break;

                case 'P':
                    // USER IS TRYING TO 'RCL' THE VARIABLE

                    if(KM_SHIFTPLANE(keymsg) & SHIFT_HOLD) {
                        //  DECOMPILE THE CONTENTS AND INSERT DIRECTLY INTO THE COMMAND LINE

                        word_p *var = rplFindGlobal(action, 1);

                        byte_p string = 0, endstring;

                        if(var) {

                            if(ISDIR(*var[1])) {
                                // VARIABLE IS A DIRECTORY, DON'T RCL
                                // BUT PUT THE NAME
                                string = (byte_p) (action + 1);
                                endstring = string + rplGetIdentLength(action);
                            }
                            else {
                                rplGetDecompiledString(var[1], DECOMP_EDIT, &string, &endstring);
                            }
                            if(string) {
                                uiSeparateToken();
                                int32_t nlines =
                                        uiInsertCharactersN(string, endstring);
                                if(nlines)
                                    uiStretchCmdLine(nlines);

                                uiSeparateToken();
                                uiAutocompleteUpdate();
                            }
                        }
                        break;

                    }
                    uiSeparateToken();
                    uiInsertCharacters((byte_p) "'");
                    uiInsertCharactersN((byte_p) (action + 1),
                            (byte_p) (action + 1) + rplGetIdentLength(action));
                    uiInsertCharacters((byte_p) "' RCL");
                    uiSeparateToken();
                    uiAutocompleteUpdate();
                    break;
                }
                break;
            }
            if(ISUNIT(*action)) {

                switch (halScreen.CursorState & 0xff) {
                case 'D':
                    if(endCmdLineAndCompile()) {
                        // FIND THE VARIABLE AGAIN, IT MIGHT'VE MOVED DUE TO GC
                        menu = uiGetLibMenu(mcode);
                        item = uiGetMenuItem(mcode, menu,
                                MENUPAGE(mcode) + varnum);
                        action = uiGetMenuItemAction(item,
                                KM_SHIFTPLANE(keymsg));

                        // USER IS TRYING TO APPLY DIVIDING
                        rplPushData(action);    // PUSH THE NAME ON THE STACK
                        Opcode = (CMD_OVR_DIV);
                    }
                    break;

                case 'A':
                case 'P':
                {
                    byte_p string, endstring;
                    if (!rplGetDecompiledString(action, DECOMP_EDIT, &string, &endstring))
                        break; // ERROR WITHIN A MENU PROGRAM! JUST IGNORE FOR NOW

                    uiSeparateToken();
                    uiInsertCharactersN(string, endstring);
                    uiSeparateToken();
                    uiInsertCharacters((byte_p) "/");
                    uiSeparateToken();
                    uiAutocompleteUpdate();

                    break;
                }

                }
                break;
            }

            if(ISLIBRARY(*action)) {
                switch (halScreen.CursorState & 0xff) {
                case 'D':
                case 'P':
                {
                    // SHOW THE LIBRARY MENU
                    int64_t libmcode =
                            (((int64_t) action[2]) << 32) | MKMENUCODE(0,
                            DOLIBPTR, 0, 0);
                    word_p numobject = rplNewint32_t(libmcode, HEXint32_t);

                    if(!numobject || Exceptions)
                        return;

                    rplPushDataNoGrow(numobject);
                    rplSaveMenuHistory(menunum);
                    rplChangeMenu(menunum, rplPopData());

                    if(menunum == 1)
                        halScreen.DirtyFlag |= MENU1_DIRTY;
                    else
                        halScreen.DirtyFlag |= MENU2_DIRTY;

                    break;
                }
                case 'A':
                {
                    // INSERT THE LIBRARY IDENTIFIER
                    byte_p string, endstring;
                    string = (byte_p) (action + 2);
                    endstring = string + rplGetIdentLength(action + 1);
                    int32_t nlines = uiInsertCharactersN(string, endstring);
                    if(nlines)
                        uiStretchCmdLine(nlines);
                    uiAutocompleteUpdate();

                }
                    break;
                }
                break;
            }

            if(ISPROGRAM(*action)) {
                if(!ISSECO(*action)) {
                    // IT'S A DOCOL PROGRAM, EXECUTE TRANSPARENTLY
                    rplPushData(action);        // PUSH THE NAME ON THE STACK
                    Opcode = CMD_OVR_XEQ;
                    break;
                }
            }

            // ALL OTHER OBJECTS AND COMMANDS, DO XEQ AFTER ENDING THE COMMAND LINE
            if(endCmdLineAndCompile()) {
                // FIND THE VARIABLE AGAIN, IT MIGHT'VE MOVED DUE TO GC
                menu = uiGetLibMenu(mcode);
                item = uiGetMenuItem(mcode, menu, MENUPAGE(mcode) + varnum);
                action = uiGetMenuItemAction(item, KM_SHIFTPLANE(keymsg));
                rplPushData(action);    // PUSH THE NAME ON THE STACK
                Opcode = CMD_OVR_XEQ;
            }

            break;
        }
        default:
        {
            // DO DIFFERENT ACTIONS BASED ON OBJECT TYPE

            if(ISIDENT(*action)) {
                switch (halScreen.CursorState & 0xff) {
                case 'D':
                {
                    // HANDLE DIRECTORIES IN A SPECIAL WAY: DON'T CLOSE THE COMMAND LINE
                    word_p *var = rplFindGlobal(action, 1);
                    if(var) {
                        if(ISDIR(*(var[1]))) {
                            // CHANGE THE DIR WITHOUT CLOSING THE COMMAND LINE
                            rplPushData(action);        // PUSH THE NAME ON THE STACK
                            Opcode = (CMD_OVR_EVAL);
                            break;
                        }
                    }

                    if(endCmdLineAndCompile()) {
                        // FIND THE VARIABLE AGAIN, IT MIGHT'VE MOVED DUE TO GC
                        menu = uiGetLibMenu(mcode);
                        item = uiGetMenuItem(mcode, menu,
                                MENUPAGE(mcode) + varnum);
                        action = uiGetMenuItemAction(item,
                                KM_SHIFTPLANE(keymsg));

                        // USER IS TRYING TO EVAL THE VARIABLE
                        rplPushData(action);    // PUSH THE NAME ON THE STACK
                        Opcode = (CMD_OVR_EVAL);
                    }
                    break;
                }
                case 'A':
                {
                    word_p *var = rplFindGlobal(action, 1);
                    if(var) {
                        if(ISDIR(*(var[1]))) {
                            // CHANGE THE DIR WITHOUT CLOSING THE COMMAND LINE
                            rplPushData(action);        // PUSH THE NAME ON THE STACK
                            Opcode = (CMD_OVR_EVAL);
                            break;
                        }
                    }
                    byte_p string, endstring;
                    if (!rplGetDecompiledStringWithoutTickmarks(action, DECOMP_EDIT, &string, &endstring))
                        break; // ERROR WITHIN A MENU PROGRAM! JUST IGNORE FOR NOW

                    uiInsertCharactersN(string, endstring);
                    uiAutocompleteUpdate();

                    break;
                }

                case 'P':
                {
                    word_p *var = rplFindGlobal(action, 1);
                    if(var) {
                        if(ISDIR(*(var[1]))) {
                            // CHANGE THE DIR WITHOUT CLOSING THE COMMAND LINE
                            rplPushData(action);        // PUSH THE NAME ON THE STACK
                            Opcode = (CMD_OVR_EVAL);
                            break;
                        }
                    }

                    byte_p string, endstring;
                    if (!rplGetDecompiledStringWithoutTickmarks(action, DECOMP_EDIT, &string, &endstring))
                        break; // ERROR WITHIN A MENU PROGRAM! JUST IGNORE FOR NOW

                    uiSeparateToken();
                    uiInsertCharactersN(string, endstring);
                    uiSeparateToken();
                    uiAutocompleteUpdate();

                    break;
                }
                }
                break;

            }
            if(ISUNIT(*action)) {
                switch (halScreen.CursorState & 0xff) {
                case 'D':
                    if(endCmdLineAndCompile()) {
                        // FIND THE VARIABLE AGAIN, IT MIGHT'VE MOVED DUE TO GC
                        menu = uiGetLibMenu(mcode);
                        item = uiGetMenuItem(mcode, menu,
                                MENUPAGE(mcode) + varnum);
                        action = uiGetMenuItemAction(item,
                                KM_SHIFTPLANE(keymsg));

                        // USER IS TRYING TO APPLY THE UNIT
                        rplPushData(action);    // PUSH THE NAME ON THE STACK
                        Opcode = (CMD_OVR_MUL);
                    }
                    break;

                case 'A':
                {

                    // DECOMPILE THE OBJECT AND INCLUDE IN COMMAND LINE
                    int32_t SavedException = Exceptions;
                    int32_t SavedErrorCode = ErrorCode;
                    int32_t removevalue;

                    if(ISNUMBER(action[1])) {
                        REAL r;
                        rplReadNumberAsReal(action + 1, &r);
                        rplOneToRReg(0);
                        removevalue = eqReal(&r, &RReg[0]);
                    }
                    else
                        removevalue = 0;

                    Exceptions = 0;     // ERASE ANY PREVIOUS ERROR TO ALLOW THE DECOMPILER TO RUN
                    // DO NOT SAVE IPtr BECAUSE IT CAN MOVE
                    word_p opname = rplDecompile(action, DECOMP_EDIT);
                    Exceptions = SavedException;
                    ErrorCode = SavedErrorCode;

                    if(!opname)
                        break;  // ERROR WITHIN A MENU PROGRAM! JUST IGNORE FOR NOW

                    byte_p string, endstring;
                    int32_t totaln = rplGetStringPointers(opname, &string, &endstring);

                    if(removevalue) {
                        // SKIP THE NUMERIC PORTION, LEAVE JUST THE UNIT
                        int32_t k, offset;
                        for(k = 0, offset = 0; k < totaln;
                                ++k, offset =
                                (byte_p) utf8skip((char *)string + offset,
                                    (char *)endstring) - string)
                            if(utf82cp((char *)string + offset,
                                        (char *)endstring) == '_') {
                                totaln -= k + 1;
                                string += offset + 1;
                                break;
                            }
                    }

                    uiInsertCharactersN(string, endstring);
                    uiAutocompleteUpdate();

                    break;
                }

                case 'P':
                {
                    byte_p string, endstring;
                    if (!rplGetDecompiledString(action, DECOMP_EDIT, &string, &endstring))
                        break; // ERROR WITHIN A MENU PROGRAM! JUST IGNORE FOR NOW

                    uiSeparateToken();
                    uiInsertCharactersN(string, endstring);
                    uiSeparateToken();
                    uiInsertCharacters((byte_p) "*");
                    uiSeparateToken();
                    uiAutocompleteUpdate();

                    break;
                }

                }
                break;
            }
            if(!ISPROLOG(*action)) {
                // THIS IS A COMMAND, DECOMPILE AND INSERT NAME
                switch (halScreen.CursorState & 0xff) {
                case 'D':
                    if(endCmdLineAndCompile()) {
                        // FIND THE COMMAND AGAIN, IT MIGHT'VE MOVED DUE TO GC
                        menu = uiGetLibMenu(mcode);
                        item = uiGetMenuItem(mcode, menu,
                                MENUPAGE(mcode) + varnum);
                        action = uiGetMenuItemAction(item,
                                KM_SHIFTPLANE(keymsg));

                        Opcode = *action;
                        hideargument = 0;
                    }
                    break;

                case 'A':
                {

                    WORD tokeninfo = 0;
                    LIBHANDLER han = rplGetLibHandler(LIBNUM(*action));

                    // GET THE SYMBOLIC TOKEN INFORMATION
                    if(han) {
                        WORD savecurOpcode = CurOpcode;
                        DecompileObject = action;
                        CurOpcode = MKOPCODE(LIBNUM(*action), OPCODE_GETINFO);
                        (*han) ();

                        if(RetNum > OK_TOKENINFO) {
                            tokeninfo = RetNum;
                        }

                        CurOpcode = savecurOpcode;
                    }

                    byte_p string, endstring;
                    if (!rplGetDecompiledString(action, DECOMP_EDIT | DECOMP_NOHINTS, &string, &endstring))
                        break; // ERROR WITHIN A MENU PROGRAM! JUST IGNORE FOR NOW

                    int32_t nlines = uiInsertCharactersN(string, endstring);
                    if(nlines)
                        uiStretchCmdLine(nlines);

                    if(TI_TYPE(tokeninfo) == TITYPE_FUNCTION) {
                        uiInsertCharacters((byte_p) "()");
                        uiCursorLeft(1);
                    }
                    uiAutocompleteUpdate();

                    break;
                }

                case 'P':
                {

                    int32_t dhints = 0;

                    if(!rplTestSystemFlag(FL_AUTOINDENT)) {

                        LIBHANDLER han = rplGetLibHandler(LIBNUM(*action));

                        // GET THE SYMBOLIC TOKEN INFORMATION
                        if(han) {
                            WORD savecurOpcode = CurOpcode;
                            DecompileObject = action;
                            CurOpcode =
                                    MKOPCODE(LIBNUM(*action), OPCODE_GETINFO);
                            (*han) ();

                            if(RetNum > OK_TOKENINFO) {
                                dhints = DecompHints;
                            }

                            CurOpcode = savecurOpcode;
                        }

                    }

                    byte_p string, endstring;
                    if (!rplGetDecompiledString(action, DECOMP_EDIT | DECOMP_NOHINTS, &string, &endstring))
                        break; // ERROR WITHIN A MENU PROGRAM! JUST IGNORE FOR NOW

                    int32_t nlines = 0;

                    if(dhints & HINT_ALLBEFORE) {
                        if(dhints & HINT_ADDINDENTBEFORE)
                            halScreen.CmdLineIndent += 2;
                        if(dhints & HINT_SUBINDENTBEFORE)
                            halScreen.CmdLineIndent -= 2;

                        if(dhints & HINT_NLBEFORE) {
                            int32_t isempty;
                            int32_t nlvl = uiGetIndentLevel(&isempty);
                            // IF THE LINE IS EMPTY DON'T ADD A NEW LINE
                            if(isempty) {
                                if(dhints & HINT_ADDINDENTBEFORE)
                                    uiInsertCharacters((byte_p) "  ");
                                if(dhints & HINT_SUBINDENTBEFORE) {
                                    if(nlvl > 2)
                                        nlvl = 2;
                                    uiCursorLeft(nlvl);
                                    uiRemoveCharacters(nlvl);
                                }
                            }
                            else {
                                uiInsertCharacters((byte_p) "\n");

                                ++nlines;
                                int k;
                                for(k = 0; k < nlvl + halScreen.CmdLineIndent;
                                        ++k)
                                    uiInsertCharacters((byte_p) " ");  // APPLY INDENT
                                halScreen.CmdLineIndent = 0;
                            }
                        }
                    }

                    uiSeparateToken();
                    nlines += uiInsertCharactersN(string, endstring);

                    if(dhints & HINT_ALLAFTER) {
                        if(dhints & HINT_ADDINDENTAFTER)
                            halScreen.CmdLineIndent += 2;
                        if(dhints & HINT_SUBINDENTAFTER)
                            halScreen.CmdLineIndent -= 2;
                        if(dhints & HINT_NLAFTER) {
                            int32_t nlvl = uiGetIndentLevel(0);

                            uiInsertCharacters((byte_p) "\n");

                            ++nlines;
                            int k;
                            for(k = 0; k < nlvl + halScreen.CmdLineIndent; ++k)
                                uiInsertCharacters((byte_p) " ");      // APPLY INDENT
                            halScreen.CmdLineIndent = 0;
                        }
                    }

                    if(nlines)
                        uiStretchCmdLine(nlines);

                    uiSeparateToken();
                    uiAutocompleteUpdate();

                    break;
                }

                }
                break;

            }

            if(ISLIBRARY(*action)) {
                // SHOW THE LIBRARY MENU
                int64_t libmcode =
                        (((int64_t) action[2]) << 32) | MKMENUCODE(0, DOLIBPTR,
                        0, 0);
                word_p numobject = rplNewint32_t(libmcode, HEXint32_t);

                if(!numobject || Exceptions)
                    return;

                rplPushDataNoGrow(numobject);
                rplSaveMenuHistory(menunum);
                rplChangeMenu(menunum, rplPopData());

                if(menunum == 1)
                    halScreen.DirtyFlag |= MENU1_DIRTY;
                else
                    halScreen.DirtyFlag |= MENU2_DIRTY;

                break;
            }

            if(ISPROGRAM(*action)) {
                if(!ISSECO(*action)) {
                    // IT'S A DOCOL PROGRAM, EXECUTE TRANSPARENTLY
                    rplPushData(action);        // PUSH THE NAME ON THE STACK
                    Opcode = CMD_OVR_XEQ;
                }
                else {
                    if(endCmdLineAndCompile()) {
                        // FIND THE VARIABLE AGAIN, IT MIGHT'VE MOVED DUE TO GC
                        menu = uiGetLibMenu(mcode);
                        item = uiGetMenuItem(mcode, menu,
                                MENUPAGE(mcode) + varnum);
                        action = uiGetMenuItemAction(item,
                                KM_SHIFTPLANE(keymsg));
                        rplPushData(action);    // PUSH THE NAME ON THE STACK
                        Opcode = CMD_OVR_XEQ;
                    }
                }
                break;
            }

            // ALL OTHER OBJECTS AND COMMANDS
            switch (halScreen.CursorState & 0xff) {
            case 'D':
                if(endCmdLineAndCompile()) {
                    // FIND THE COMMAND AGAIN, IT MIGHT'VE MOVED DUE TO GC
                    menu = uiGetLibMenu(mcode);
                    item = uiGetMenuItem(mcode, menu, MENUPAGE(mcode) + varnum);
                    action = uiGetMenuItemAction(item, KM_SHIFTPLANE(keymsg));
                    if(!ISPROLOG(*action)) {
                        Opcode = *action;       // RUN COMMANDS DIRECTLY
                        hideargument = 0;
                    }
                    else {
                        Opcode = (CMD_OVR_XEQ);
                        rplPushData(action);
                    }
                }
                break;

            case 'A':
            {

                WORD tokeninfo = 0;
                LIBHANDLER han = rplGetLibHandler(LIBNUM(*action));

                // GET THE SYMBOLIC TOKEN INFORMATION
                if(han) {
                    WORD savecurOpcode = CurOpcode;
                    DecompileObject = action;
                    CurOpcode = MKOPCODE(LIBNUM(*action), OPCODE_GETINFO);
                    (*han) ();

                    if(RetNum > OK_TOKENINFO)
                        tokeninfo = RetNum;

                    CurOpcode = savecurOpcode;
                }

                byte_p string, endstring;
                if (!rplGetDecompiledString(action, DECOMP_EDIT | DECOMP_NOHINTS, &string, &endstring))
                    break; // ERROR WITHIN A MENU PROGRAM! JUST IGNORE FOR NOW

                uiInsertCharactersN(string, endstring);
                if(TI_TYPE(tokeninfo) == TITYPE_FUNCTION) {
                    uiInsertCharacters((byte_p) "()");
                    uiCursorLeft(1);
                }
                uiAutocompleteUpdate();

                break;
            }

            case 'P':
            {
                byte_p string, endstring;
                if (!rplGetDecompiledString(action, DECOMP_EDIT, &string, &endstring))
                    break; // ERROR WITHIN A MENU PROGRAM! JUST IGNORE FOR NOW

                uiSeparateToken();
                int32_t nlines = uiInsertCharactersN(string, endstring);
                if(nlines)
                    uiStretchCmdLine(nlines);

                uiSeparateToken();
                uiAutocompleteUpdate();

                break;
            }

            }

            break;

        }
        }

        if(Opcode)
            uiCmdRunHide(Opcode, hideargument);
        if(Exceptions) {
            // TODO: SHOW ERROR MESSAGE
            halShowErrorMsg();
            Exceptions = 0;
        }
        else
            halScreen.DirtyFlag |= MENU1_DIRTY | MENU2_DIRTY;
        halScreen.DirtyFlag |= STACK_DIRTY | STAREA_DIRTY;
    }

}

void symbolKeyHandler(WORD keymsg, byte_p symbol, int32_t separate)
{
    if(!(halGetContext() & CONTEXT_INEDITOR)) {
        if(halGetContext() >> 5)
            return;     // DO NOTHING INSIDE A FORM, UNLESS THE EDITOR IS OPEN
        if(halGetContext() & CONTEXT_INTSTACK)
            return;     // DO NOTHING
        halSetCmdLineHeight(CMDLINE_HEIGHT);
        halSetContext(halGetContext() | CONTEXT_INEDITOR);
        if(KM_SHIFTPLANE(keymsg) & SHIFT_ALPHA)
            uiOpenCmdLine('X');
        else
            uiOpenCmdLine('D');
    }

    if(separate && ((halScreen.CursorState & 0xff) == 'P'))
        uiSeparateToken();

    uiInsertCharacters(symbol);

    if(separate && ((halScreen.CursorState & 0xff) == 'P'))
        uiSeparateToken();
    uiAutocompleteUpdate();

}

void alphasymbolKeyHandler(WORD keymsg, byte_p Lsymbol, byte_p Csymbol)
{
    if(!(halGetContext() & CONTEXT_INEDITOR)) {
        if(halGetContext() >> 5)
            return;     // DO NOTHING INSIDE A FORM, UNLESS THE EDITOR IS OPEN

        if(halGetContext() & CONTEXT_INTSTACK)
            return;     // DO NOTHING

        halSetCmdLineHeight(CMDLINE_HEIGHT);
        halSetContext(halGetContext() | CONTEXT_INEDITOR);
        if(KM_SHIFTPLANE(keymsg) & SHIFT_ALPHA)
            uiOpenCmdLine('X');
        else
            uiOpenCmdLine('D');
    }

    if(halGetCmdLineMode() == 'L')
        uiInsertCharacters(Lsymbol);
    if(halGetCmdLineMode() == 'C')
        uiInsertCharacters(Csymbol);
    uiAutocompleteUpdate();

}

void VarMenuKeyHandler(WORD keymsg)
{
    UNUSED(keymsg);

    // SIMPLY TOGGLE THE MENU UPON PRESS
    if(halScreen.Menu2)
        halSetMenu2Height(0);
    else
        halSetMenu2Height(MENU2_HEIGHT);

}

void newlineKeyHandler(WORD keymsg)
{
    if(!(halGetContext() & CONTEXT_INEDITOR)) {
        if(halGetContext() >> 5)
            return;     // DO NOTHING INSIDE A FORM, UNLESS THE EDITOR IS OPEN
        if(halGetContext() & CONTEXT_INTSTACK)
            return;     // DO NOTHING

        halSetCmdLineHeight(CMDLINE_HEIGHT);
        halSetContext(halGetContext() | CONTEXT_INEDITOR);
        if(KM_SHIFTPLANE(keymsg) & SHIFT_ALPHA)
            uiOpenCmdLine('X');
        else
            uiOpenCmdLine('D');

    }

    // INCREASE THE HEIGHT ON-SCREEN UP TO THE MAXIMUM
    uiStretchCmdLine(1);
    int32_t ilvl = uiGetIndentLevel(0);

    // ADD A NEW LINE
    uiInsertCharacters((byte_p) "\n");
    int k;
    for(k = 0; k < ilvl + halScreen.CmdLineIndent; ++k)
        uiInsertCharacters((byte_p) " ");
    halScreen.CmdLineIndent = 0;

    uiAutocompleteUpdate();

}

void decimaldotKeyHandler(WORD keymsg)
{
    UNUSED(keymsg);
    if(!(halGetContext() & CONTEXT_INEDITOR)) {
        if(halGetContext() >> 5)
            return;     // DO NOTHING INSIDE A FORM, UNLESS THE EDITOR IS OPEN

        if(halGetContext() & CONTEXT_INTSTACK)
            return;     // DO NOTHING

        halSetCmdLineHeight(CMDLINE_HEIGHT);
        halSetContext(halGetContext() | CONTEXT_INEDITOR);
        if(KM_SHIFTPLANE(keymsg) & SHIFT_ALPHA)
            uiOpenCmdLine('X');
        else
            uiOpenCmdLine('D');
    }

    uint64_t Locale = rplGetSystemLocale();

    WORD ucode = cp2utf8(DECIMAL_DOT(Locale));
    if(ucode & 0xff000000)
        uiInsertCharactersN((byte_p) & ucode, ((byte_p) & ucode) + 4);
    else
        uiInsertCharacters((byte_p) & ucode);

    uiAutocompleteUpdate();

}

void enterKeyHandler(WORD keymsg)
{
    UNUSED(keymsg);

    if(!(halGetContext() & CONTEXT_INEDITOR)) {
        // PERFORM DUP
        if(halGetContext() & CONTEXT_STACK) {
            // PERFORM DUP ONLY IF THERE'S DATA ON THE STACK
            // DON'T ERROR IF STACK IS EMPTY
            if(rplDepthData() > 0)
                uiCmdRun(CMD_DUP);
            halScreen.DirtyFlag |= STACK_DIRTY;
        }

        if(halGetContext() & CONTEXT_INTSTACK) {
            // COPY THE ELEMENT TO LEVEL ONE (PICK)
            if((halScreen.StkPointer > 0)
                    && (halScreen.StkPointer <= rplDepthData())) {
                rplPushData(rplPeekData(halScreen.StkPointer));
                ++halScreen.StkPointer;
                halScreen.StkVisibleLvl = -1;
                halScreen.DirtyFlag |= STACK_DIRTY;
            }
        }

    }
    else {
        // ENABLE UNDO
        if(halScreen.StkCurrentLevel != 1)
            rplTakeSnapshot();
        halScreen.StkCurrentLevel = 0;

        if(endCmdLineAndCompile()) {
            halScreen.DirtyFlag |=
                    STACK_DIRTY | MENU1_DIRTY | MENU2_DIRTY | STAREA_DIRTY;
            if(!(halFlags & (HAL_HWRESET | HAL_RESET))) {
                rplRemoveSnapshot(halScreen.StkUndolevels + 2);
                rplRemoveSnapshot(halScreen.StkUndolevels + 1);
            }

        }
        else {
            // SOMETHING WENT WRONG DURING COMPILE, STACK DIDN'T CHANGE
            rplRemoveSnapshot(1);

        }

    }
}

void cutclipKeyHandler(WORD keymsg)
{
    UNUSED(keymsg);

    if(!(halGetContext() & CONTEXT_INEDITOR)) {
        if(halGetContext() & CONTEXT_STACK) {
            // ACTION WHEN IN THE STACK
            uiCmdRunTransparent(CMD_CUTCLIP, 1, 1);
            if(Exceptions) {
                // TODO: SHOW ERROR MESSAGE
                halShowErrorMsg();
                Exceptions = 0;
            }
            else
                halScreen.DirtyFlag |= MENU1_DIRTY | MENU2_DIRTY;
            halScreen.DirtyFlag |= STACK_DIRTY;
            return;
        }
        if(halGetContext() & CONTEXT_INTSTACK) {
            int32_t selst, selend;
            switch (halScreen.StkSelStatus) {
            case 0:    // NO ITEMS SELECTED
                if((halScreen.StkPointer < 1)
                        || (halScreen.StkPointer > rplDepthData()))
                    return;
                selst = selend = halScreen.StkPointer;
                break;
            case 1:    // 1 ITEM SELECTED, SELECT ALL ITEMS BETWEEN POINTER AND SELSTART
                if(halScreen.StkPointer > halScreen.StkSelStart) {
                    selst = halScreen.StkSelStart;
                    selend = (halScreen.StkPointer <
                            rplDepthData())? halScreen.
                            StkPointer : rplDepthData();
                }
                else {
                    selend = halScreen.StkSelStart;
                    selst = (halScreen.StkPointer <
                            1) ? 1 : halScreen.StkPointer;
                }
                break;
            case 2:    // BOTH START AND END SELECTED
                selst = halScreen.StkSelStart;
                selend = halScreen.StkSelEnd;
                break;
            default:
                return;
            }

            // PUT ALL OBJECTS IN A LIST

            if(selend - selst == 0) {
                // SINGLE OBJECT, JUST PUT IN THE CLIPBOARD
                rplPushData(rplPeekData(selst));
                uiCmdRunTransparent(CMD_CUTCLIP, 1, 1);
                if(Exceptions) {
                    // TODO: SHOW ERROR MESSAGE
                    halShowErrorMsg();
                    Exceptions = 0;
                }
                else
                    halScreen.DirtyFlag |= MENU1_DIRTY | MENU2_DIRTY;
                halScreen.DirtyFlag |= STACK_DIRTY;
                rplRemoveAtData(selst, 1);

                if(rplDepthData() < 1) {
                    // END INTERACTIVE STACK
                    halSetContext((halGetContext() & ~CONTEXT_INTSTACK) |
                            CONTEXT_STACK);
                    halScreen.StkVisibleLvl = 1;
                    halScreen.StkVisibleOffset = 0;
                    halScreen.StkSelStart = halScreen.StkSelEnd =
                            halScreen.StkSelStatus = 0;

                }
                else {

                    halScreen.StkVisibleLvl = -1;
                    halScreen.StkSelStatus = 0;
                    if(halScreen.StkPointer > selend)
                        halScreen.StkPointer--;
                    else if(halScreen.StkPointer >= selst)
                        halScreen.StkPointer = (selst > 1) ? (selst - 1) : 1;

                }

                return;

            }

            word_p newlist = rplCreateListN(selend - selst + 1, selst, 0);
            if((!newlist) || Exceptions)
                return;
            rplListAutoExpand(newlist);

            rplPushData(newlist);
            uiCmdRunTransparent(CMD_CUTCLIP, 1, 1);
            if(Exceptions) {
                // TODO: SHOW ERROR MESSAGE
                halShowErrorMsg();
                Exceptions = 0;
            }
            else
                halScreen.DirtyFlag |= MENU1_DIRTY | MENU2_DIRTY;
            halScreen.DirtyFlag |= STACK_DIRTY;

            rplRemoveAtData(selst, selend - selst + 1);

            if(rplDepthData() < 1) {
                // END INTERACTIVE STACK
                halSetContext((halGetContext() & ~CONTEXT_INTSTACK) |
                        CONTEXT_STACK);
                halScreen.StkVisibleLvl = 1;
                halScreen.StkVisibleOffset = 0;
                halScreen.StkSelStart = halScreen.StkSelEnd =
                        halScreen.StkSelStatus = 0;

            }
            else {

                // DISABLE SELECTION STATUS
                halScreen.StkSelStatus = 0;
                if(halScreen.StkPointer > selend)
                    halScreen.StkPointer -= selend - selst + 1;
                else if(halScreen.StkPointer >= selst)
                    halScreen.StkPointer = (selst > 1) ? (selst - 1) : 1;

            }
            return;

        }

    }
    else {
        // ACTION INSIDE THE EDITOR
        word_p string = uiExtractSelection();

        if(string) {
            rplPushData(string);
            uiCmdRunTransparent(CMD_CUTCLIP, 1, 0);
            if(Exceptions) {
                // TODO: SHOW ERROR MESSAGE
                halShowErrorMsg();
                Exceptions = 0;
            }
            else
                halScreen.DirtyFlag |= MENU1_DIRTY | MENU2_DIRTY;

            uiDeleteSelection();
            halScreen.DirtyFlag |= STACK_DIRTY;
        }

    }
}

void copyclipKeyHandler(WORD keymsg)
{
    UNUSED(keymsg);

    if(!(halGetContext() & CONTEXT_INEDITOR)) {
        if(halGetContext() & CONTEXT_STACK) {
            // ACTION WHEN IN THE STACK
            uiCmdRunTransparent(CMD_COPYCLIP, 1, 1);
            if(Exceptions) {
                // TODO: SHOW ERROR MESSAGE
                halShowErrorMsg();
                Exceptions = 0;
            }
            else
                halScreen.DirtyFlag |= MENU1_DIRTY | MENU2_DIRTY;
            halScreen.DirtyFlag |= STACK_DIRTY;
        }
        if(halGetContext() & CONTEXT_INTSTACK) {
            int32_t selst, selend;
            switch (halScreen.StkSelStatus) {
            case 0:    // NO ITEMS SELECTED
                if((halScreen.StkPointer < 1)
                        || (halScreen.StkPointer > rplDepthData()))
                    return;
                selst = selend = halScreen.StkPointer;
                break;
            case 1:    // 1 ITEM SELECTED, SELECT ALL ITEMS BETWEEN POINTER AND SELSTART
                if(halScreen.StkPointer > halScreen.StkSelStart) {
                    selst = halScreen.StkSelStart;
                    selend = (halScreen.StkPointer <
                            rplDepthData())? halScreen.
                            StkPointer : rplDepthData();
                }
                else {
                    selend = halScreen.StkSelStart;
                    selst = (halScreen.StkPointer <
                            1) ? 1 : halScreen.StkPointer;
                }
                break;
            case 2:    // BOTH START AND END SELECTED
                selst = halScreen.StkSelStart;
                selend = halScreen.StkSelEnd;
                break;
            default:
                return;
            }

            // PUT ALL OBJECTS IN A LIST

            if(selend - selst == 0) {
                // SINGLE OBJECT, JUST PUT IN THE CLIPBOARD
                rplPushData(rplPeekData(selst));
                uiCmdRunTransparent(CMD_COPYCLIP, 1, 1);
                rplDropData(1);
                if(Exceptions) {
                    // TODO: SHOW ERROR MESSAGE
                    halShowErrorMsg();
                    Exceptions = 0;
                }
                else
                    halScreen.DirtyFlag |= MENU1_DIRTY | MENU2_DIRTY;
                halScreen.DirtyFlag |= STACK_DIRTY;
                return;

            }

            word_p newlist = rplCreateListN(selend - selst + 1, selst, 0);
            if((!newlist) || Exceptions)
                return;

            // MAKE NEW LIST AUTOEXPAND ON PASTE
            rplListAutoExpand(newlist);

            rplPushData(newlist);
            uiCmdRunTransparent(CMD_COPYCLIP, 1, 1);
            rplDropData(1);

            if(Exceptions) {
                // TODO: SHOW ERROR MESSAGE
                halShowErrorMsg();
                Exceptions = 0;
            }
            else
                halScreen.DirtyFlag |= MENU1_DIRTY | MENU2_DIRTY;
            halScreen.DirtyFlag |= STACK_DIRTY;

            return;

        }

    }
    else {
        // ACTION INSIDE THE EDITOR
        word_p string = uiExtractSelection();

        if(string) {
            rplPushData(string);
            uiCmdRunTransparent(CMD_CUTCLIP, 1, 0);
            if(Exceptions) {
                // TODO: SHOW ERROR MESSAGE
                halShowErrorMsg();
                Exceptions = 0;
            }
            else
                halScreen.DirtyFlag |= MENU1_DIRTY | MENU2_DIRTY;
            halScreen.DirtyFlag |= STACK_DIRTY;
        }

    }
}

void pasteclipKeyHandler(WORD keymsg)
{
    UNUSED(keymsg);

    if(!(halGetContext() & CONTEXT_INEDITOR)) {
        if(halGetContext() & CONTEXT_STACK) {
            // ACTION WHEN IN THE STACK
            uiCmdRun(CMD_PASTECLIP);
            if(Exceptions) {
                halShowErrorMsg();
                Exceptions = 0;
            }
            else
                halScreen.DirtyFlag |= MENU1_DIRTY | MENU2_DIRTY;
            halScreen.DirtyFlag |= STACK_DIRTY;
        }

        if(halGetContext() & CONTEXT_INTSTACK) {
            int32_t depth = rplDepthData();
            int32_t clevel =
                    (halScreen.StkPointer >
                    depth) ? depth : halScreen.StkPointer;

            uiCmdRun(CMD_PASTECLIP);
            int32_t nitems = rplDepthData() - depth;
            if(Exceptions) {
                halShowErrorMsg();
                Exceptions = 0;
                return;
            }
            else
                halScreen.DirtyFlag |= MENU1_DIRTY | MENU2_DIRTY;
            halScreen.DirtyFlag |= STACK_DIRTY;

            // NOW MOVE THE NEW OBJECT TO THE CURRENT LEVEL

            rplExpandStack(nitems);

            if(Exceptions) {
                halShowErrorMsg();
                Exceptions = 0;
                return;
            }

            // MAKE ROOM
            memmovew(DSTop - clevel, DSTop - clevel - nitems,
                    (clevel + nitems) * sizeof(word_p) / sizeof(WORD));
            // MOVE THE OBJECTS
            memmovew(DSTop - clevel - nitems, DSTop,
                    nitems * sizeof(word_p) / sizeof(WORD));

            if(halScreen.StkSelStatus) {
                if(halScreen.StkSelStart > clevel)
                    halScreen.StkSelStart += nitems;
                if(halScreen.StkSelEnd > clevel)
                    halScreen.StkSelEnd += nitems;
            }
            halScreen.StkPointer++;

            halScreen.StkVisibleLvl = -1;

        }

    }
    else {
        // ACTION INSIDE THE EDITOR
        int32_t depth = rplDepthData();
        uiCmdRun(CMD_PASTECLIP);
        int32_t nitems = rplDepthData() - depth;
        while(nitems >= 1) {
            word_p object = rplPeekData(nitems);
            if(!ISSTRING(*object)) {
                object = rplDecompile(object, DECOMP_EDIT);
                if(!object || Exceptions) {
                    halShowErrorMsg();
                    Exceptions = 0;
                    return;
                }
                if(((halScreen.CursorState & 0xff) == 'P')
                        || ((halScreen.CursorState & 0xff) == 'D'))
                    uiSeparateToken();
            }

            rplRemoveAtData(nitems, 1);
            uiInsertCharactersN((byte_p) (object + 1),
                    (byte_p) (object + 1) + rplStrSize(object));
            --nitems;
        }

    }
}

void backspKeyHandler(WORD keymsg)
{
    UNUSED(keymsg);

    if(!(halGetContext() & CONTEXT_INEDITOR)) {
        // DO DROP
        if(halGetContext() & CONTEXT_STACK) {
            // PERFORM DROP ONLY IF THERE'S DATA ON THE STACK
            // DON'T ERROR IF STACK IS EMPTY
            if(rplDepthData() > 0)
                uiCmdRun(CMD_DROP);
            halScreen.DirtyFlag |= STACK_DIRTY;
        }

        if(halGetContext() & CONTEXT_INTSTACK) {
            // SELECTION MODE
            switch (halScreen.StkSelStatus) {
            case 0:
                // NOTHING SELECTED YET, DROP CURRENT ELEMENT
                if(halScreen.StkPointer > rplDepthData())
                    break;
                if(rplDepthData() == 1) {
                    // DROP THE OBJECT AND END THE INTERACTIVE STACK
                    rplDropData(1);

                    // END INTERACTIVE STACK
                    halSetContext((halGetContext() & ~CONTEXT_INTSTACK) |
                            CONTEXT_STACK);
                    halScreen.StkVisibleLvl = 1;
                    halScreen.StkVisibleOffset = 0;
                    halScreen.StkSelStart = halScreen.StkSelEnd =
                            halScreen.StkSelStatus = 0;
                    halScreen.DirtyFlag |= STACK_DIRTY;

                    return;
                }
                if(halScreen.StkPointer <= 0)
                    return;
                if(halScreen.StkPointer == 1)
                    rplDropData(1);
                else {
                    rplRemoveAtData(halScreen.StkPointer, 1);
                    if(halScreen.StkPointer > rplDepthData())
                        halScreen.StkPointer = rplDepthData();
                }
                halScreen.StkVisibleLvl = -1;
                halScreen.DirtyFlag |= STACK_DIRTY;

                break;
            case 1:
                // START WAS SELECTED, DELETE EVERYTHING HIGHLIGHTED

                if(halScreen.StkPointer > halScreen.StkSelStart) {
                    int32_t count =
                            ((halScreen.StkPointer >
                                rplDepthData())? rplDepthData() : halScreen.
                            StkPointer) - halScreen.StkSelStart + 1;
                    if(rplDepthData() <= count) {
                        // COMPLETELY CLEAR THE STACK AND END INTERACTIVE MODE
                        rplClearData();

                        // END INTERACTIVE STACK
                        halSetContext((halGetContext() & ~CONTEXT_INTSTACK) |
                                CONTEXT_STACK);
                        halScreen.StkVisibleLvl = 1;
                        halScreen.StkVisibleOffset = 0;
                        halScreen.StkSelStart = halScreen.StkSelEnd =
                                halScreen.StkSelStatus = 0;
                        halScreen.DirtyFlag |= STACK_DIRTY;

                        return;

                    }
                    rplRemoveAtData(halScreen.StkSelStart, count);
                    halScreen.StkPointer -= count;
                    if(halScreen.StkPointer < 1)
                        halScreen.StkPointer = 1;
                    halScreen.StkSelStatus = 0;
                    halScreen.StkVisibleLvl = -1;

                }
                else {
                    if(rplDepthData() ==
                            (halScreen.StkPointer ? halScreen.StkPointer : 1) -
                            halScreen.StkSelStart + 1) {
                        // COMPLETELY CLEAR THE STACK AND END INTERACTIVE MODE
                        rplClearData();

                        // END INTERACTIVE STACK
                        halSetContext((halGetContext() & ~CONTEXT_INTSTACK) |
                                CONTEXT_STACK);
                        halScreen.StkVisibleLvl = 1;
                        halScreen.StkVisibleOffset = 0;
                        halScreen.StkSelStart = halScreen.StkSelEnd =
                                halScreen.StkSelStatus = 0;
                        halScreen.DirtyFlag |= STACK_DIRTY;

                        return;

                    }

                    if(halScreen.StkPointer <= 1) {
                        // JUST DROP ALL ITEMS
                        rplDropData(halScreen.StkSelStart);
                    }
                    else {
                        rplRemoveAtData(halScreen.StkPointer,
                                halScreen.StkSelStart - halScreen.StkPointer +
                                1);
                    }
                    if(halScreen.StkPointer > rplDepthData())
                        halScreen.StkPointer = rplDepthData();
                    halScreen.StkSelStatus = 0;
                    halScreen.StkVisibleLvl = -1;

                }

                halScreen.DirtyFlag |= STACK_DIRTY;
                break;
            case 2:
            {
                // BOTH START AND END SELECTED, DELETE SELECTED ITEMS
                if(rplDepthData() ==
                        halScreen.StkSelEnd - halScreen.StkSelStart + 1) {
                    // COMPLETELY CLEAR THE STACK AND END INTERACTIVE MODE
                    rplClearData();

                    // END INTERACTIVE STACK
                    halSetContext((halGetContext() & ~CONTEXT_INTSTACK) |
                            CONTEXT_STACK);
                    halScreen.StkVisibleLvl = 1;
                    halScreen.StkVisibleOffset = 0;
                    halScreen.StkSelStart = halScreen.StkSelEnd =
                            halScreen.StkSelStatus = 0;
                    halScreen.DirtyFlag |= STACK_DIRTY;

                    return;

                }

                int32_t count = halScreen.StkSelEnd - halScreen.StkSelStart + 1;
                rplRemoveAtData(halScreen.StkSelStart, count);
                if(halScreen.StkPointer > halScreen.StkSelEnd)
                    halScreen.StkPointer -= count;
                else if(halScreen.StkPointer >= halScreen.StkSelStart)
                    halScreen.StkPointer = halScreen.StkSelStart;
                if(halScreen.StkPointer > rplDepthData())
                    halScreen.StkPointer = rplDepthData();
                halScreen.StkSelStatus = 0;
                halScreen.StkVisibleLvl = -1;

                halScreen.DirtyFlag |= STACK_DIRTY;
                break;
            }
            }

        }

    }
    else {
        // REMOVE CHARACTERS FROM THE COMMAND LINE

        uiCursorLeft(1);
        uiRemoveCharacters(1);
        uiAutocompleteUpdate();
    }
}

void deleteKeyHandler(WORD keymsg)
{
    UNUSED(keymsg);

    if((halGetContext() & CONTEXT_INEDITOR)) {
        // REMOVE CHARACTERS FROM THE COMMAND LINE
        uiRemoveCharacters(1);
    }
}

void leftKeyHandler(WORD keymsg)
{
    UNUSED(keymsg);

    if(!(halGetContext() & CONTEXT_INEDITOR)) {
        if(halGetContext() & CONTEXT_STACK) {
            // PERFORM UNDO IN THE STACK
            uiStackUndo();
            halScreen.DirtyFlag |= STACK_DIRTY | STAREA_DIRTY;
            return;

        }
        if(halGetContext() & CONTEXT_INTSTACK) {
            switch (halScreen.StkSelStatus) {
            case 0:
                // NO ITEM SELECTED, ROT WITH LEVEL 1
                if(rplDepthData() >= halScreen.StkPointer) {
                    word_p *stptr, *endptr, *cptr;
                    word_p item;
                    stptr = DSTop - halScreen.StkPointer;
                    endptr = DSTop - 1;

                    cptr = stptr;

                    item = *cptr;

                    while(cptr < endptr) {
                        cptr[0] = cptr[1];
                        ++cptr;
                    }
                    *cptr = item;
                }
                break;

            case 1:
            {
                // ONE ITEM SELECTED, ROT THE WHOLE BLOCK BETWEEN POINTER AND SELSTART
                word_p *stptr, *endptr;

                if(halScreen.StkPointer > halScreen.StkSelStart) {
                    endptr = DSTop - halScreen.StkSelStart;
                    stptr = DSTop - ((halScreen.StkPointer >=
                                rplDepthData())? rplDepthData() : halScreen.
                            StkPointer);
                }
                else {
                    stptr = DSTop - halScreen.StkSelStart;
                    endptr = DSTop - halScreen.StkPointer;
                }

                // NOW ROT BETWEEN THEM
                word_p *cptr = stptr;
                word_p item = *stptr;
                while(cptr < endptr) {
                    cptr[0] = cptr[1];
                    ++cptr;
                }
                *cptr = item;
                break;
            }

            case 2:
                // START AND END SELECTED, MOVE THE BLOCK TO THE CURSOR
            {
                if(halScreen.StkPointer > halScreen.StkSelEnd) {
                    word_p *stptr, *endptr, *cptr;
                    word_p item;
                    stptr = DSTop - halScreen.StkSelStart;
                    endptr = DSTop - ((halScreen.StkPointer >
                                rplDepthData())? rplDepthData() : halScreen.
                            StkPointer);

                    // DO UNROT UNTIL THE ENTIRE BLOCK MOVED
                    int32_t count =
                            halScreen.StkSelEnd - halScreen.StkSelStart + 1;

                    while(count--) {
                        cptr = stptr;

                        item = *cptr;

                        while(cptr > endptr) {
                            cptr[0] = cptr[-1];
                            --cptr;
                        }
                        *cptr = item;
                    }

                    count = halScreen.StkSelEnd - halScreen.StkSelStart;
                    halScreen.StkSelEnd =
                            ((halScreen.StkPointer >
                                rplDepthData())? rplDepthData() : halScreen.
                            StkPointer);
                    halScreen.StkSelStart = halScreen.StkSelEnd - count;

                    break;

                }
                if(halScreen.StkPointer < halScreen.StkSelStart) {

                    word_p *stptr, *endptr, *cptr;
                    word_p item;
                    stptr = DSTop - halScreen.StkSelEnd;
                    endptr = DSTop - halScreen.StkPointer - 1;

                    // DO ROT UNTIL THE ENTIRE BLOCK MOVED
                    int32_t count =
                            halScreen.StkSelEnd - halScreen.StkSelStart + 1;
                    while(count--) {
                        cptr = stptr;

                        item = *cptr;

                        while(cptr < endptr) {
                            cptr[0] = cptr[1];
                            ++cptr;
                        }
                        *cptr = item;
                    }

                    count = halScreen.StkSelEnd - halScreen.StkSelStart;
                    halScreen.StkSelStart = halScreen.StkPointer + 1;
                    halScreen.StkSelEnd = halScreen.StkPointer + 1 + count;
                    halScreen.StkPointer += count + 1;
                    halScreen.StkVisibleLvl = -1;

                    break;
                }

                // WHEN THE POINTER IS WITHIN THE BLOCK JUST UNROT THE BLOCK
                word_p *stptr, *endptr, *cptr;
                word_p item;
                stptr = DSTop - halScreen.StkSelStart;
                endptr = DSTop - halScreen.StkSelEnd;

                cptr = stptr;

                item = *cptr;

                while(cptr > endptr) {
                    cptr[0] = cptr[-1];
                    --cptr;
                }
                *cptr = item;

                break;

            }

            }
            halScreen.DirtyFlag |= STACK_DIRTY;
            return;

        }

    }
    else {
        int32_t line = halScreen.LineCurrent;
        uiCursorLeft(1);
        if(line != halScreen.LineCurrent)
            halScreen.CmdLineIndent = 0;

        halDeferProcess(&uiAutocompleteUpdate);
    }
}

void rsleftKeyHandler(WORD keymsg)
{
    UNUSED(keymsg);

    if(!(halGetContext() & CONTEXT_INEDITOR)) {
        if(halGetContext() & CONTEXT_STACK) {
            // REDO ACTION
            uiStackRedo();
            halScreen.DirtyFlag |= STACK_DIRTY | STAREA_DIRTY;
            return;
        }

    }
    else {
        uiCursorStartOfLine();
        uiAutocompleteUpdate();
    }
}

void rsholdleftKeyHandler(WORD keymsg)
{
    UNUSED(keymsg);

    if(!(halGetContext() & CONTEXT_INEDITOR)) {
        if(halGetContext() & CONTEXT_STACK) {
            // REDO ACTION
            uiStackRedo();
            halScreen.DirtyFlag |= STACK_DIRTY | STAREA_DIRTY;
            return;

        }

    }
    else {
        uiCursorPageLeft();
        uiAutocompleteUpdate();
    }
}

void lsleftKeyHandler(WORD keymsg)
{
    UNUSED(keymsg);

    if(!(halGetContext() & CONTEXT_INEDITOR)) {
        if(halGetContext() & CONTEXT_STACK) {
            // TODO: WHAT TO DO WITH RS-LEFT CURSOR??
            // THIS SHOULD SCROLL A LARGE OBJECT IN LEVEL 1

        }

    }
    else {
        uiSetSelectionStart();
    }
}

void rightKeyHandler(WORD keymsg)
{
    UNUSED(keymsg);

    if(!(halGetContext() & CONTEXT_INEDITOR)) {
        if(halGetContext() & CONTEXT_STACK) {

            if(rplDepthData() > 1) {
                uiCmdRun(CMD_SWAP);
                halScreen.DirtyFlag |= STACK_DIRTY;
            }

        }
        if(halGetContext() & CONTEXT_INTSTACK) {
            switch (halScreen.StkSelStatus) {
            case 0:
            {
                if(rplDepthData() >= halScreen.StkPointer) {

                    // NO ITEM SELECTED, UNROT THE WHOLE BLOCK BETWEEN POINTER AND LEVEL 1
                    word_p *stptr, *endptr;

                    stptr = DSTop - 1;
                    endptr = DSTop -
                            (halScreen.StkPointer ? halScreen.StkPointer : 1);

                    // NOW UNROT BETWEEN THEM
                    word_p *cptr = stptr;
                    word_p item = *stptr;
                    while(cptr > endptr) {
                        cptr[0] = cptr[-1];
                        --cptr;
                    }
                    *cptr = item;
                }
                break;
            }
            case 1:
            {
                // ONE ITEM SELECTED, UNROT THE WHOLE BLOCK BETWEEN POINTER AND SELSTART
                word_p *stptr, *endptr;

                if(halScreen.StkPointer > halScreen.StkSelStart) {
                    stptr = DSTop - halScreen.StkSelStart;
                    endptr = DSTop - ((halScreen.StkPointer >
                                rplDepthData())? rplDepthData() : halScreen.
                            StkPointer);
                }
                else {
                    endptr = DSTop - halScreen.StkSelStart;
                    stptr = DSTop -
                            (halScreen.StkPointer ? halScreen.StkPointer : 1);
                }

                // NOW UNROT BETWEEN THEM
                word_p *cptr = stptr;
                word_p item = *stptr;
                while(cptr > endptr) {
                    cptr[0] = cptr[-1];
                    --cptr;
                }
                *cptr = item;
                break;
            }

            case 2:
                // START AND END SELECTED, COPY THE BLOCK TO THE CURSOR
            {
                if(halScreen.StkPointer > halScreen.StkSelEnd) {
                    // MAKE HOLE
                    int32_t count =
                            halScreen.StkSelEnd - halScreen.StkSelStart + 1;
                    int32_t stkptr = halScreen.StkPointer;
                    if(halScreen.StkPointer > rplDepthData())
                        stkptr = rplDepthData();

                    rplExpandStack(count);
                    if(Exceptions)
                        return;

                    memmovew(DSTop - stkptr + count, DSTop - stkptr,
                            stkptr * sizeof(word_p) / sizeof(WORD));

                    // AND COPY THE SELECTION
                    memmovew(DSTop - stkptr,
                            DSTop - halScreen.StkSelEnd + count,
                            count * sizeof(word_p) / sizeof(WORD));

                    DSTop += count;
                    halScreen.StkPointer += count;
                    halScreen.StkVisibleLvl = -1;

                    break;

                }
                if(halScreen.StkPointer < halScreen.StkSelStart) {

                    // MAKE HOLE
                    int32_t count =
                            halScreen.StkSelEnd - halScreen.StkSelStart + 1;

                    rplExpandStack(count);
                    if(Exceptions)
                        return;

                    memmovew(DSTop - halScreen.StkPointer + count,
                            DSTop - halScreen.StkPointer,
                            halScreen.StkPointer * sizeof(word_p) /
                            sizeof(WORD));

                    // AND COPY THE SELECTION
                    memmovew(DSTop - halScreen.StkPointer,
                            DSTop - halScreen.StkSelEnd,
                            count * sizeof(word_p) / sizeof(WORD));

                    DSTop += count;
                    halScreen.StkPointer += count;
                    halScreen.StkSelStart += count;
                    halScreen.StkSelEnd += count;
                    halScreen.StkVisibleLvl = -1;

                    break;
                }

                // WHEN THE POINTER IS WITHIN THE BLOCK JUST ROT THE BLOCK
                word_p *stptr, *endptr, *cptr;
                word_p item;
                endptr = DSTop - halScreen.StkSelStart;
                stptr = DSTop - halScreen.StkSelEnd;

                cptr = stptr;

                item = *cptr;

                while(cptr < endptr) {
                    cptr[0] = cptr[1];
                    ++cptr;
                }
                *cptr = item;

                break;

            }

            }
            halScreen.DirtyFlag |= STACK_DIRTY;
            return;

        }
    }
    else {
        int32_t line = halScreen.LineCurrent;
        uiCursorRight(1);
        if(line != halScreen.LineCurrent)
            halScreen.CmdLineIndent = 0;

        halDeferProcess(&uiAutocompleteUpdate);
    }
}

void rsrightKeyHandler(WORD keymsg)
{
    UNUSED(keymsg);

    if(!(halGetContext() & CONTEXT_INEDITOR)) {
        if(halGetContext() & CONTEXT_STACK) {
            // TODO: WHAT TO DO WITH RS-RIGHT CURSOR??
            // THIS SHOULD SCROLL A LARGE OBJECT IN LEVEL 1

        }

    }
    else {
        uiCursorEndOfLine();
        uiAutocompleteUpdate();
    }
}

void lsrightKeyHandler(WORD keymsg)
{
    UNUSED(keymsg);

    if(!(halGetContext() & CONTEXT_INEDITOR)) {
        if(halGetContext() & CONTEXT_STACK) {
            // NOT SURE WHAT TO DO WITH THIS KEY

        }

    }
    else {
        // IN THE EDITOR, DO SELECTION
        uiSetSelectionEnd();
    }
}

void rsholdrightKeyHandler(WORD keymsg)
{
    UNUSED(keymsg);

    if(!(halGetContext() & CONTEXT_INEDITOR)) {
        if(halGetContext() & CONTEXT_STACK) {
            // TODO: WHAT TO DO WITH RS-RIGHT CURSOR??
            // THIS SHOULD SCROLL A LARGE OBJECT IN LEVEL 1

        }

    }
    else {
        uiCursorPageRight();
        uiAutocompleteUpdate();
    }
}

void alphaholdrightKeyHandler(WORD keymsg)
{
    UNUSED(keymsg);

    if(!(halGetContext() & CONTEXT_INEDITOR)) {
        if(halGetContext() & CONTEXT_STACK) {
            // TODO: ??
        }
        // TODO: ADD OTHER CONTEXTS HERE
    }

    else {
        // GO UP ONE LINE IN MULTILINE TEXT EDITOR
        uiAutocompInsert();
        uiAutocompleteUpdate();
    }
}

void downKeyHandler(WORD keymsg)
{
    UNUSED(keymsg);

    if(!(halGetContext() & CONTEXT_INEDITOR)) {
        if(halGetContext() & CONTEXT_STACK) {

            if(rplDepthData() >= 1) {
                word_p prefwidth = rplGetSettings((word_p) editwidth_ident);
                int32_t width;
                if(!prefwidth)
                    width = 0;
                else
                    width = rplReadNumberAsInt64(prefwidth);
                if(Exceptions) {
                    width = 0;
                    Exceptions = 0;
                }
                word_p ptr = rplPeekData(1);
                word_p text =
                        rplDecompile(ptr, DECOMP_EDIT | DECOMP_MAXWIDTH(width));
                if(Exceptions) {
                    halShowErrorMsg();
                    Exceptions = 0;
                    return;
                }
                BYTE cursorstart = 'D';

                if(ISPROGRAM(*ptr))
                    cursorstart = 'P';
                if(ISSYMBOLIC(*ptr))
                    cursorstart = 'A';
                if(ISUNIT(*ptr))
                    cursorstart = 'A';
                if(ISLIST(*ptr))
                    cursorstart = 'P';

                // OPEN THE COMMAND LINE
                halSetCmdLineHeight(CMDLINE_HEIGHT);
                halSetContext(halGetContext() | CONTEXT_INEDITOR);
                if(KM_SHIFTPLANE(keymsg) & SHIFT_ALPHA)
                    uiOpenCmdLine('X');
                else
                    uiOpenCmdLine(cursorstart);
                int32_t lines = uiSetCmdLineText(text);
                if(lines > 1) {
                    uiStretchCmdLine(lines - 1);
                    halScreen.LineVisible = 1;
                    uiEnsureCursorVisible();
                }

                uiSetCmdLineState(uiGetCmdLineState() | CMDSTATE_OVERWRITE);
                return;
            }

        }

        if(halGetContext() & CONTEXT_INTSTACK) {
            if(halScreen.StkPointer > 0) {
                --halScreen.StkPointer;
                halScreen.StkVisibleLvl = -1;
                halScreen.DirtyFlag |= STACK_DIRTY;
            }
            return;
        }

        // TODO: ADD OTHER CONTEXTS HERE
    }

    else {
        // GO DOWN ONE LINE IN MULTILINE TEXT EDITOR
        uiCursorDown(1);
        halScreen.CmdLineIndent = 0;
        halDeferProcess(&uiAutocompleteUpdate);
    }
}

void rsholddownKeyHandler(WORD keymsg)
{
    UNUSED(keymsg);

    if(!(halGetContext() & CONTEXT_INEDITOR)) {
        if(halGetContext() & CONTEXT_STACK) {
            // TODO: ??
        }
        if(halGetContext() & CONTEXT_INTSTACK) {
            if(halScreen.StkPointer > 1) {
                halScreen.StkPointer = halScreen.StkVisibleLvl - 1;
                if(halScreen.StkPointer < 1)
                    halScreen.StkPointer = 1;
                halScreen.StkVisibleLvl = -1;
                halScreen.DirtyFlag |= STACK_DIRTY;
            }
            return;
        }
        // TODO: ADD OTHER CONTEXTS HERE

    }

    else {
        // GO UP ONE LINE IN MULTILINE TEXT EDITOR
        uiCursorPageDown();
        halScreen.CmdLineIndent = 0;

        halDeferProcess(&uiAutocompleteUpdate);
    }
}

void rsdownKeyHandler(WORD keymsg)
{
    UNUSED(keymsg);

    if(!(halGetContext() & CONTEXT_INEDITOR)) {
        if(halGetContext() & CONTEXT_STACK) {
            // TODO: ??
        }
        if(halGetContext() & CONTEXT_INTSTACK) {
            if(halScreen.StkPointer > 1) {
                halScreen.StkPointer = 1;
                halScreen.StkVisibleLvl = 1;
                halScreen.StkVisibleOffset = 0;
                halScreen.DirtyFlag |= STACK_DIRTY;
            }
            return;
        }

        // TODO: ADD OTHER CONTEXTS HERE
    }

    else {
        // GO UP ONE LINE IN MULTILINE TEXT EDITOR
        uiCursorEndOfText();
        halScreen.CmdLineIndent = 0;

        uiAutocompleteUpdate();
    }
}

void alphaholddownKeyHandler(WORD keymsg)
{
    UNUSED(keymsg);

    if(!(halGetContext() & CONTEXT_INEDITOR)) {
        if(halGetContext() & CONTEXT_STACK) {
            // TODO: ??
        }
        // TODO: ADD OTHER CONTEXTS HERE
    }

    else {
        // GO UP ONE LINE IN MULTILINE TEXT EDITOR
        uiAutocompNext();
    }
}

void upKeyHandler(WORD keymsg)
{
    UNUSED(keymsg);

    if(!(halGetContext() & CONTEXT_INEDITOR)) {
        if(halGetContext() & CONTEXT_STACK) {
            // START INTERACTIVE STACK MANIPULATION HERE

            if(rplDepthData() > 0) {

                // ENABLE UNDO
                rplRemoveSnapshot(halScreen.StkUndolevels + 1);
                rplRemoveSnapshot(halScreen.StkUndolevels);
                if(halScreen.StkCurrentLevel != 1)
                    rplTakeSnapshot();
                halScreen.StkCurrentLevel = 0;

                halSetContext((halGetContext() & ~CONTEXT_STACK) |
                        CONTEXT_INTSTACK);

                halScreen.StkPointer = 1;
                halScreen.StkSelStart = halScreen.StkSelEnd = -1;
                halScreen.StkVisibleLvl = 1;
                halScreen.StkVisibleOffset = 0;
                halScreen.DirtyFlag |= STACK_DIRTY;
                halScreen.StkSelStatus = 0;
            }
            return;
        }
        if(halGetContext() & CONTEXT_INTSTACK) {
            if(halScreen.StkPointer <= rplDepthData()) {
                ++halScreen.StkPointer;
                halScreen.StkVisibleLvl = -1;
                halScreen.DirtyFlag |= STACK_DIRTY;
            }
            return;
        }
        // TODO: ADD OTHER CONTEXTS HERE
    }

    else {
        // GO UP ONE LINE IN MULTILINE TEXT EDITOR
        uiCursorUp(1);
        halScreen.CmdLineIndent = 0;
        halDeferProcess(&uiAutocompleteUpdate);

    }
}

void rsholdupKeyHandler(WORD keymsg)
{
    UNUSED(keymsg);

    if(!(halGetContext() & CONTEXT_INEDITOR)) {
        if(halGetContext() & CONTEXT_STACK) {
            // TODO: START INTERACTIVE STACK MANIPULATION HERE
        }
        if(halGetContext() & CONTEXT_INTSTACK) {
            if(halScreen.StkPointer < rplDepthData()) {
                halScreen.StkPointer += 5;
                if(halScreen.StkPointer >= rplDepthData())
                    halScreen.StkPointer = rplDepthData();
                halScreen.StkVisibleLvl = -1;
                halScreen.DirtyFlag |= STACK_DIRTY;
            }
            return;
        }

        // TODO: ADD OTHER CONTEXTS HERE
    }

    else {
        // GO UP ONE LINE IN MULTILINE TEXT EDITOR
        uiCursorPageUp();
        halScreen.CmdLineIndent = 0;
        halDeferProcess(&uiAutocompleteUpdate);

    }
}

void rsupKeyHandler(WORD keymsg)
{
    UNUSED(keymsg);

    if(!(halGetContext() & CONTEXT_INEDITOR)) {
        if(halGetContext() & CONTEXT_STACK) {
            // TODO: START INTERACTIVE STACK MANIPULATION HERE
        }
        if(halGetContext() & CONTEXT_INTSTACK) {
            if(halScreen.StkPointer != rplDepthData()) {
                halScreen.StkPointer = rplDepthData();
                halScreen.StkVisibleLvl = -1;
                halScreen.DirtyFlag |= STACK_DIRTY;
            }
            return;
        }

        // TODO: ADD OTHER CONTEXTS HERE
    }

    else {
        // GO UP ONE LINE IN MULTILINE TEXT EDITOR
        uiCursorStartOfText();
        halScreen.CmdLineIndent = 0;

        uiAutocompleteUpdate();
    }
}

void alphaholdupKeyHandler(WORD keymsg)
{
    UNUSED(keymsg);

    if(!(halGetContext() & CONTEXT_INEDITOR)) {
        if(halGetContext() & CONTEXT_STACK) {
            // TODO: ??
        }
        // TODO: ADD OTHER CONTEXTS HERE
    }

    else {
        // GO UP ONE LINE IN MULTILINE TEXT EDITOR
        uiAutocompPrev();
    }
}

void chsKeyHandler(WORD keymsg)
{
    UNUSED(keymsg);

    if(!(halGetContext() & CONTEXT_INEDITOR)) {
        if(halGetContext() & CONTEXT_STACK) {
            // ACTION WHEN IN THE STACK
            uiCmdRun((CMD_OVR_NEG));
            if(Exceptions) {
                // TODO: SHOW ERROR MESSAGE
                halShowErrorMsg();
                Exceptions = 0;
            }
            halScreen.DirtyFlag |= STACK_DIRTY;
        }
        if(halGetContext() & CONTEXT_INTSTACK) {
            // SELECTION MODE
            switch (halScreen.StkSelStatus) {
            case 0:
                // NOTHING SELECTED YET
                halScreen.StkSelStart =
                        (halScreen.StkPointer ? halScreen.StkPointer : 1);
                if(halScreen.StkSelStart > rplDepthData())
                    halScreen.StkSelStart = rplDepthData();
                halScreen.StkSelEnd = halScreen.StkSelStart;
                halScreen.StkSelStatus += 2;
                halScreen.DirtyFlag |= STACK_DIRTY;
                break;
            case 1:
                // START WAS SELECTED
                if(halScreen.StkSelStart > halScreen.StkPointer) {
                    halScreen.StkSelEnd = halScreen.StkSelStart;
                    halScreen.StkSelStart =
                            (halScreen.StkPointer ? halScreen.StkPointer : 1);
                }
                else {
                    halScreen.StkSelEnd =
                            (halScreen.StkPointer ? halScreen.StkPointer : 1);
                    if(halScreen.StkSelEnd > rplDepthData())
                        halScreen.StkSelEnd = rplDepthData();
                }
                ++halScreen.StkSelStatus;
                halScreen.DirtyFlag |= STACK_DIRTY;
                break;
            case 2:
                // BOTH START AND END SELECTED, REPLACE SELECTION WITH NEW ITEM
                halScreen.StkSelStart =
                        (halScreen.StkPointer ? halScreen.StkPointer : 1);
                if(halScreen.StkSelStart > rplDepthData())
                    halScreen.StkSelStart = rplDepthData();
                halScreen.StkSelEnd = halScreen.StkSelStart;
                halScreen.DirtyFlag |= STACK_DIRTY;
                break;
            }

        }

    }
    else {
        // ACTION INSIDE THE EDITOR

        byte_p startnum;
        byte_p endnum;
        int32_t flags;
        byte_p line;

        // FIRST CASE: IF TOKEN UNDER THE CURSOR IS OR CONTAINS A VALID NUMBER, CHANGE THE SIGN OF THE NUMBER IN THE TEXT
        startnum = uiFindNumberStart(&endnum, &flags);
        line = (byte_p) (CmdLineCurrentLine + 1);
        if(!startnum) {
            startnum = line + halScreen.CursorPosition;
            if(startnum > line) {
                if(startnum[-1] == '+') {
                    uiCursorLeft(1);
                    uiRemoveCharacters(1);
                    uiInsertCharacters((byte_p) "-");
                    halScreen.DirtyFlag |=
                            CMDLINE_LINEDIRTY | CMDLINE_CURSORDIRTY;
                    return;
                }
                if(startnum[-1] == '-') {
                    uiCursorLeft(1);
                    uiRemoveCharacters(1);
                    uiInsertCharacters((byte_p) "+");
                    halScreen.DirtyFlag |=
                            CMDLINE_LINEDIRTY | CMDLINE_CURSORDIRTY;
                    return;
                }
                if((startnum[-1] == 'E') || (startnum[-1] == 'e')) {
                    if(startnum[0] == '+') {
                        uiRemoveCharacters(1);
                        uiInsertCharacters((byte_p) "-");
                        uiAutocompleteUpdate();
                        return;
                    }
                    else if(startnum[0] == '-') {
                        uiRemoveCharacters(1);
                        uiInsertCharacters((byte_p) "+");
                        uiAutocompleteUpdate();
                        return;
                    }
                    else {
                        uiInsertCharacters((byte_p) "+");
                        uiAutocompleteUpdate();
                        return;
                    }
                }

            }

            // SECOND CASE: IF TOKEN UNDER CURSOR IS EMPTY, IN 'D' MODE COMPILE OBJECT AND THEN EXECUTE NEG

            if((halScreen.CursorState & 0xff) == 'D') {
                // COMPILE AND EXECUTE NEG
                if(endCmdLineAndCompile()) {
                    uiCmdRun((CMD_OVR_NEG));
                    if(Exceptions) {
                        // TODO: SHOW ERROR MESSAGE
                        halShowErrorMsg();
                        Exceptions = 0;
                    }
                    halScreen.DirtyFlag |= STACK_DIRTY;
                }

                return;
            }

            if((halScreen.CursorState & 0xff) == 'P') {
                uiSeparateToken();
                uiInsertCharacters((byte_p) "NEG");
                uiSeparateToken();
                uiAutocompleteUpdate();
                return;
            }

            if((halScreen.CursorState & 0xff) == 'A') {
                // IN ALGEBRAIC MODE, TRY TO CHANGE THE SIGN OF THE LAST + OR - SIGN TO THE LEFT OF THE CURSOR

                startnum = line + halScreen.CursorPosition;
                int moveleft = 0;
                byte_p prevstnum = startnum;
                startnum =
                        (byte_p) utf8rskipst((char *)startnum, (char *)line);
                if(startnum != prevstnum)
                    ++moveleft;
                int32_t char1, char2;
                extern const const char forbiddenChars[];
                while(startnum >= line) {
                    byte_p ptr = (byte_p) forbiddenChars;
                    char1 = utf82cp((char *)startnum, (char *)prevstnum);
                    do {
                        char2 = utf82cp((char *)ptr, (char *)ptr + 4);
                        if(char1 == char2)
                            break;
                        ptr = (byte_p) utf8skip((char *)ptr, (char *)ptr + 4);
                    }
                    while(*ptr);
                    if(*ptr)
                        break;
                    if(*startnum == '\'')
                        break;
                    byte_p newptr =
                            (byte_p) utf8rskipst((char *)startnum,
                            (char *)line);
                    if(newptr == startnum)
                        break;  // COULDN'T SKIP ANYMORE
                    ++moveleft;
                    prevstnum = startnum;
                    startnum = newptr;
                }
                if(*startnum == '+') {
                    if(moveleft > 0)
                        uiCursorLeft(moveleft);
                    uiRemoveCharacters(1);
                    uiInsertCharacters((byte_p) "-");
                    if(moveleft > 0)
                        uiCursorRight(moveleft - 1);
                }
                else {
                    if(*startnum == '-') {
                        if(moveleft > 0)
                            uiCursorLeft(moveleft);
                        uiRemoveCharacters(1);
                        uiInsertCharacters((byte_p) "+");
                        if(moveleft > 0)
                            uiCursorRight(moveleft - 1);
                    }
                    else {
                        // FOUND NOTHING!
                        if(moveleft > 0)
                            uiCursorLeft(moveleft - 1);
                        else
                            uiCursorRight(1);
                        startnum =
                                (byte_p) utf8skipst((char *)startnum,
                                (char *)(startnum + 4));
                        if(*startnum == '+') {
                            uiRemoveCharacters(1);
                            uiInsertCharacters((byte_p) "-");
                        }
                        else if(*startnum == '-') {
                            uiRemoveCharacters(1);
                            uiInsertCharacters((byte_p) "+");
                        }
                        else
                            uiInsertCharacters((byte_p) "-");

                        if(moveleft > 0)
                            uiCursorRight(moveleft - 1);

                    }
                }

                uiAutocompleteUpdate();
                return;
            }

        }
        else {
            // WE FOUND A NUMBER
            int32_t oldposition = halScreen.CursorPosition;
            // IF THIS IS A NUMBER WITH AN EXPONENT, SEE IF WE NEED TO CHANGE THE SIGN OF THE NUMBER OR THE EXPONENT
            if((flags >> 16) & 4) {
                // LOOK FOR THE 'e' OR 'E'
                int32_t epos = 0;
                while((epos < endnum - startnum) && ((startnum[epos] != 'E')
                            && (startnum[epos] != 'e')))
                    ++epos;

                if(oldposition > (startnum - line) + epos)
                    startnum += epos + 1;       // MOVE START OF NUMBER TO AFTER THE EXPONENT LETTER
                if((startnum[0] == '-') || (startnum[0] == '+'))
                    ++startnum;
            }
            uiMoveCursor(startnum - line);
            byte_p plusminus = (byte_p) "-";

            if(startnum > line) {
                if(startnum[-1] == '+') {
                    uiMoveCursor(startnum - line - 1);
                    uiRemoveCharacters(1);
                    --oldposition;
                }
                if(startnum[-1] == '-') {
                    uiMoveCursor(startnum - line - 1);
                    uiRemoveCharacters(1);
                    plusminus = (byte_p) "+";
                    --oldposition;
                }
            }

            // NEED TO INSERT A CHARACTER HERE
            uiInsertCharacters(plusminus);
            uiMoveCursor(oldposition + 1);
            uiEnsureCursorVisible();
            uiAutocompleteUpdate();
            return;
        }

        // THIRD CASE: IF TOKEN UNDER CURSOR IS SOMETHING OTHER THAN A NUMBER, JUST INSERT A MINUS SIGN

    }
}

void eexKeyHandler(WORD keymsg)
{

    if(!(halGetContext() & CONTEXT_INEDITOR)) {
        if(halGetContext() & CONTEXT_STACK) {
            halSetCmdLineHeight(CMDLINE_HEIGHT);
            halSetContext(halGetContext() | CONTEXT_INEDITOR);
            if(KM_SHIFTPLANE(keymsg) & SHIFT_ALPHA)
                uiOpenCmdLine('X');
            else
                uiOpenCmdLine('D');
            NUMFORMAT config;

            rplGetSystemNumberFormat(&config);
            if((config.MiddleFmt | config.BigFmt | config.
                        SmallFmt) & FMT_USECAPITALS)
                uiInsertCharacters((byte_p) "1E");
            else
                uiInsertCharacters((byte_p) "1e");
            uiAutocompleteUpdate();
            return;
        }

    }
    else {
        // ACTION INSIDE THE EDITOR

        // FIRST CASE: IF TOKEN UNDER THE CURSOR IS OR CONTAINS A VALID NUMBER
        byte_p startnum, endnum;
        int32_t flags;
        NUMFORMAT config;

        rplGetSystemNumberFormat(&config);

        startnum = uiFindNumberStart(&endnum, &flags);

        byte_p line = (byte_p) (CmdLineCurrentLine + 1);

        if(!startnum) {
            startnum = line + halScreen.CursorPosition;
            // DO NOTHING IF THERE'S ALREADY AN 'E' BEFORE THE CURSOR
            if((startnum > line) && ((startnum[-1] == 'E')
                        || (startnum[-1] == 'e')))
                return;

            // SECOND CASE: IF TOKEN UNDER CURSOR IS EMPTY, IN 'D' MODE COMPILE OBJECT AND THEN APPEND 1E
            if((config.MiddleFmt | config.BigFmt | config.
                        SmallFmt) & FMT_USECAPITALS)
                uiInsertCharacters((byte_p) "1E");
            else
                uiInsertCharacters((byte_p) "1e");
            uiAutocompleteUpdate();
            return;
        }
        else {
            // WE FOUND A NUMBER
            if((startnum > line) && ((startnum[-1] == '-')
                        || (startnum[-1] == '+')))
                --startnum;

            if(halScreen.CursorPosition <= endnum + 1 - line) {
                // THE CURSOR IS WITHIN THE NUMBER
                if((flags >> 16) & 4) {
                    // THE NUMBER ALREADY HAS AN EXPONENT, LOOK FOR THE 'e' OR 'E'
                    int32_t epos = 0;
                    while((epos < endnum - startnum) && ((startnum[epos] != 'E')
                                && (startnum[epos] != 'e')))
                        ++epos;

                    startnum += epos + 1;       // MOVE START OF NUMBER TO AFTER THE EXPONENT LETTER
                    uiMoveCursor(startnum - line);
                    uiRemoveCharacters(endnum - startnum + 1);
                    uiEnsureCursorVisible();
                    uiAutocompleteUpdate();
                    return;
                }

                // NEED TO INSERT A CHARACTER HERE
                int32_t oldposition = halScreen.CursorPosition;
                if((*endnum == 'e') || (*endnum == 'E'))
                    uiMoveCursor(endnum - line + 1);
                else {
                    if((config.MiddleFmt | config.BigFmt | config.
                                SmallFmt) & FMT_USECAPITALS)
                        uiInsertCharacters((byte_p) "E");
                    else
                        uiInsertCharacters((byte_p) "e");
                    uiMoveCursor(oldposition + 1);
                }
                uiEnsureCursorVisible();
                uiAutocompleteUpdate();
                return;

            }
            // THE CURSOR WAS PAST THE END OF THE NUMBER
            if((config.MiddleFmt | config.BigFmt | config.
                        SmallFmt) & FMT_USECAPITALS)
                uiInsertCharacters((byte_p) "1E");
            else
                uiInsertCharacters((byte_p) "1e");
            uiAutocompleteUpdate();
            return;
        }

    }
}

// COMMON FUNCTION FOR AL "BRACKET TYPES"
void bracketKeyHandler(WORD keymsg, byte_p string)
{
    if(!(halGetContext() & CONTEXT_INEDITOR)) {
        if(halGetContext() & CONTEXT_INTSTACK)
            return;     // DO NOTHING

        halSetCmdLineHeight(CMDLINE_HEIGHT);
        halSetContext(halGetContext() | CONTEXT_INEDITOR);
        if(KM_SHIFTPLANE(keymsg) & SHIFT_ALPHA)
            uiOpenCmdLine('X');
        else
            uiOpenCmdLine('D');
    }
    if(((halScreen.CursorState & 0xff) == 'D')
            || ((halScreen.CursorState & 0xff) == 'P'))
        uiSeparateToken();

    byte_p end = string + stringlen((char *)string);
    uiInsertCharactersN(string, end);
    uiCursorLeft(utf8nlenst((char *)string, (char *)end) >> 1);
    uiAutocompleteUpdate();

}

void curlyBracketKeyHandler(WORD keymsg)
{
    if((halGetCmdLineMode() == 'A') || (halGetCmdLineMode() == 'C')
            || (halGetCmdLineMode() == 'L'))
        bracketKeyHandler(keymsg, (byte_p) "{}");
    else {
        bracketKeyHandler(keymsg, (byte_p) "{  }");
        halSetCmdLineMode('P');
    }

}

void squareBracketKeyHandler(WORD keymsg)
{
    if((halGetCmdLineMode() == 'A') || (halGetCmdLineMode() == 'C')
            || (halGetCmdLineMode() == 'L'))
        bracketKeyHandler(keymsg, (byte_p) "[]");
    else
        bracketKeyHandler(keymsg, (byte_p) "[  ]");

}

void secoBracketKeyHandler(WORD keymsg)
{
    bracketKeyHandler(keymsg, (byte_p) "«  »");

    if((halGetCmdLineMode() != 'L') && (halGetCmdLineMode() != 'C'))
        halSetCmdLineMode('P');

}

void parenBracketKeyHandler(WORD keymsg)
{
    bracketKeyHandler(keymsg, (byte_p) "()");

}

void textBracketKeyHandler(WORD keymsg)
{
    bracketKeyHandler(keymsg, (byte_p) "\"\"");

    //  LOCK ALPHA MODE
    if((halGetCmdLineMode() != 'L') && (halGetCmdLineMode() != 'C'))
        keyb_setshiftplane(0, 0, 1, 1);

}

void ticksKeyHandler(WORD keymsg)
{
    if((halGetCmdLineMode() != 'L') && (halGetCmdLineMode() != 'C')) {
        bracketKeyHandler(keymsg, (byte_p) "''");
        halSetCmdLineMode('A');
    }
    else
        symbolKeyHandler(keymsg, (byte_p) "'", 0);
}

void tagKeyHandler(WORD keymsg)
{
    if((halGetCmdLineMode() != 'L') && (halGetCmdLineMode() != 'C')) {
        bracketKeyHandler(keymsg, (byte_p) "::");
        //  LOCK ALPHA MODE
        keyb_setshiftplane(0, 0, 1, 1);
    }
    else
        symbolKeyHandler(keymsg, (byte_p) ":", 0);

}

void onPlusKeyHandler(WORD keymsg)
{
    UNUSED(keymsg);

    halStatusAreaPopup();

    // INCREASE CONTRAST
    gglsurface scr;
    ggl_initscr(&scr);
    int ytop =
            halScreen.Form + halScreen.Stack + halScreen.CmdLine +
            halScreen.Menu1;
    // CLEAR STATUS AREA
    ggl_rect(&scr, STATUS_AREA_X, ytop, LCD_W - 1,
             ytop + halScreen.Menu2 - 1, ggl_solid(PAL_STA_BG));

    int j;
    for(j = 0; j < 15; ++j) {
        ggl_rect(&scr, STATUS_AREA_X + 1 + 3 * j, ytop + 7,
                STATUS_AREA_X + 1 + 3 * j + 2, ytop + 12, ggl_color(j));
        ggl_rect(&scr, STATUS_AREA_X + 1 + 3 * j, ytop,
                STATUS_AREA_X + 1 + 3 * j + 2, ytop + 5, ggl_color(15 - j));
    }

    lcd_contrast++;
    if(lcd_contrast > 0xf)
        lcd_contrast = 0xf;

    lcd_setcontrast(lcd_contrast);
    WORD savedex = Exceptions;
    Exceptions = 0;
    word_p contrast = rplNewSINT(lcd_contrast, DECint32_t);
    if(contrast)
        rplStoreSettings((word_p) screenconfig_ident, contrast);
    Exceptions = savedex;
}

void onMinusKeyHandler(WORD keymsg)
{
    UNUSED(keymsg);

    halStatusAreaPopup();

    // DECREASE CONTRAST
    gglsurface scr;
    ggl_initscr(&scr);
    int ytop =
            halScreen.Form + halScreen.Stack + halScreen.CmdLine +
            halScreen.Menu1;
    // CLEAR STATUS AREA
    ggl_rect(&scr, STATUS_AREA_X, ytop, LCD_W - 1,
             ytop + halScreen.Menu2 - 1, ggl_color(PAL_STA_BG));

    int j;
    for(j = 0; j < 15; ++j) {
        ggl_rect(&scr, STATUS_AREA_X + 1 + 3 * j, ytop + 7,
                STATUS_AREA_X + 1 + 3 * j + 2, ytop + 12, ggl_color(j));
        ggl_rect(&scr, STATUS_AREA_X + 1 + 3 * j, ytop,
                STATUS_AREA_X + 1 + 3 * j + 2, ytop + 5, ggl_color(15 - j));
    }

    lcd_contrast--;
    if(lcd_contrast < 0)
        lcd_contrast = 0;
    lcd_setcontrast(lcd_contrast);
    WORD savedex = Exceptions;
    Exceptions = 0;
    word_p contrast = rplNewSINT(lcd_contrast, DECint32_t);
    if(contrast)
        rplStoreSettings((word_p) screenconfig_ident, contrast);
    Exceptions = savedex;
}

void onDotKeyHandler(WORD keymsg)
{
    UNUSED(keymsg);

    // CYCLE BETWEEN VARIOUS OPTIONS
    const char *const options[] = {
        "1000.000000", "1,000.000000", "1 000.000000", "1000.000 000",
                "1,000.000 000", "1 000.000 000",
        "1000,000000", "1.000,000000", "1 000,000000", "1000,000 000",
                "1.000,000 000", "1 000,000 000"
    };

    NUMFORMAT fmt;
    int32_t option = 0;
    rplGetSystemNumberFormat(&fmt);
    if(DECIMAL_DOT(fmt.Locale) == ',')
        option += 6;
    if(fmt.MiddleFmt & FMT_NUMSEPARATOR) {
        if(THOUSAND_SEP(fmt.Locale) == ',')
            option += 1;
        if(THOUSAND_SEP(fmt.Locale) == '.')
            option += 1;
        if(THOUSAND_SEP(fmt.Locale) == THIN_SPACE)
            option += 2;
    }
    if(fmt.MiddleFmt & FMT_FRACSEPARATOR)
        option += 3;

    // CYCLE THROUGH ITEMS:
    ++option;
    if(option > 11)
        option = 0;

    halStatusAreaPopup();

    gglsurface scr;
    ggl_initscr(&scr);
    int ytop =
        halScreen.Form + halScreen.Stack + halScreen.CmdLine +
        halScreen.Menu1;
    // CLEAR STATUS AREA
    ggl_rect(&scr, STATUS_AREA_X, ytop, LCD_W - 1,
             ytop + halScreen.Menu2 - 1, ggl_color(PAL_STA_BG));

    DrawTextBk(STATUS_AREA_X + 1, ytop + 1, "Format:",
               FONT_STATUS, ggl_color(PAL_STA_TEXT), ggl_color(PAL_STA_BG), &scr);
    DrawTextBk(STATUS_AREA_X + 1,
               ytop + 1 + FONT_HEIGHT(FONT_STATUS),
               (char *)options[option], FONT_STATUS,ggl_color(PAL_STA_TEXT), ggl_color(PAL_STA_BG),
               &scr);

    // CHANGE THE FORMAT TO THE SELECTED OPTION
    switch (option) {
    default:
    case 0:
        fmt.BigFmt &= ~(FMT_NUMSEPARATOR | FMT_FRACSEPARATOR);
        fmt.SmallFmt &= ~(FMT_NUMSEPARATOR | FMT_FRACSEPARATOR);
        fmt.MiddleFmt &= ~(FMT_NUMSEPARATOR | FMT_FRACSEPARATOR);
        fmt.Locale = MAKELOCALE('.', THIN_SPACE, THIN_SPACE, ',');
        break;
    case 1:
        fmt.BigFmt &= ~(FMT_FRACSEPARATOR);
        fmt.BigFmt |= FMT_NUMSEPARATOR;
        fmt.SmallFmt &= ~(FMT_FRACSEPARATOR);
        fmt.SmallFmt |= FMT_NUMSEPARATOR;
        fmt.MiddleFmt &= ~(FMT_FRACSEPARATOR);
        fmt.MiddleFmt |= FMT_NUMSEPARATOR;
        fmt.Locale = MAKELOCALE('.', ',', THIN_SPACE, ';');
        break;
    case 2:
        fmt.BigFmt &= ~(FMT_FRACSEPARATOR);
        fmt.BigFmt |= FMT_NUMSEPARATOR;
        fmt.SmallFmt &= ~(FMT_FRACSEPARATOR);
        fmt.SmallFmt |= FMT_NUMSEPARATOR;

        fmt.MiddleFmt &= ~(FMT_FRACSEPARATOR);
        fmt.MiddleFmt |= FMT_NUMSEPARATOR;
        fmt.Locale = MAKELOCALE('.', THIN_SPACE, THIN_SPACE, ',');
        break;
    case 3:
        fmt.BigFmt &= ~FMT_NUMSEPARATOR;
        fmt.BigFmt |= FMT_FRACSEPARATOR;
        fmt.SmallFmt &= ~FMT_NUMSEPARATOR;
        fmt.SmallFmt |= FMT_FRACSEPARATOR;
        fmt.MiddleFmt &= ~FMT_NUMSEPARATOR;
        fmt.MiddleFmt |= FMT_FRACSEPARATOR;
        fmt.Locale = MAKELOCALE('.', THIN_SPACE, THIN_SPACE, ',');
        break;
    case 4:
        fmt.BigFmt |= FMT_NUMSEPARATOR | FMT_FRACSEPARATOR;
        fmt.SmallFmt |= FMT_NUMSEPARATOR | FMT_FRACSEPARATOR;
        fmt.MiddleFmt |= FMT_NUMSEPARATOR | FMT_FRACSEPARATOR;
        fmt.Locale = MAKELOCALE('.', ',', THIN_SPACE, ';');
        break;
    case 5:
        fmt.BigFmt |= FMT_NUMSEPARATOR | FMT_FRACSEPARATOR;
        fmt.SmallFmt |= FMT_NUMSEPARATOR | FMT_FRACSEPARATOR;
        fmt.MiddleFmt |= FMT_NUMSEPARATOR | FMT_FRACSEPARATOR;
        fmt.Locale = MAKELOCALE('.', THIN_SPACE, THIN_SPACE, ',');
        break;
    case 6:
        fmt.BigFmt &= ~(FMT_NUMSEPARATOR | FMT_FRACSEPARATOR);
        fmt.SmallFmt &= ~(FMT_NUMSEPARATOR | FMT_FRACSEPARATOR);
        fmt.MiddleFmt &= ~(FMT_NUMSEPARATOR | FMT_FRACSEPARATOR);
        fmt.Locale = MAKELOCALE(',', THIN_SPACE, THIN_SPACE, ';');
        break;
    case 7:
        fmt.BigFmt &= ~(FMT_FRACSEPARATOR);
        fmt.BigFmt |= FMT_NUMSEPARATOR;
        fmt.SmallFmt &= ~(FMT_FRACSEPARATOR);
        fmt.SmallFmt |= FMT_NUMSEPARATOR;
        fmt.MiddleFmt &= ~(FMT_FRACSEPARATOR);
        fmt.MiddleFmt |= FMT_NUMSEPARATOR;
        fmt.Locale = MAKELOCALE(',', '.', THIN_SPACE, ';');
        break;
    case 8:
        fmt.BigFmt &= ~(FMT_FRACSEPARATOR);
        fmt.BigFmt |= FMT_NUMSEPARATOR;
        fmt.SmallFmt &= ~(FMT_FRACSEPARATOR);
        fmt.SmallFmt |= FMT_NUMSEPARATOR;
        fmt.MiddleFmt &= ~(FMT_FRACSEPARATOR);
        fmt.MiddleFmt |= FMT_NUMSEPARATOR;
        fmt.Locale = MAKELOCALE(',', THIN_SPACE, THIN_SPACE, ';');
        break;
    case 9:
        fmt.BigFmt &= ~FMT_NUMSEPARATOR;
        fmt.BigFmt |= FMT_FRACSEPARATOR;
        fmt.SmallFmt &= ~FMT_NUMSEPARATOR;
        fmt.SmallFmt |= FMT_FRACSEPARATOR;
        fmt.MiddleFmt &= ~FMT_NUMSEPARATOR;
        fmt.MiddleFmt |= FMT_FRACSEPARATOR;
        fmt.Locale = MAKELOCALE(',', THIN_SPACE, THIN_SPACE, ';');
        break;
    case 10:
        fmt.BigFmt |= FMT_NUMSEPARATOR | FMT_FRACSEPARATOR;
        fmt.SmallFmt |= FMT_NUMSEPARATOR | FMT_FRACSEPARATOR;
        fmt.MiddleFmt |= FMT_NUMSEPARATOR | FMT_FRACSEPARATOR;
        fmt.Locale = MAKELOCALE(',', '.', THIN_SPACE, ';');
        break;
    case 11:
        fmt.BigFmt |= FMT_NUMSEPARATOR | FMT_FRACSEPARATOR;
        fmt.SmallFmt |= FMT_NUMSEPARATOR | FMT_FRACSEPARATOR;
        fmt.MiddleFmt |= FMT_NUMSEPARATOR | FMT_FRACSEPARATOR;
        fmt.Locale = MAKELOCALE(',', THIN_SPACE, THIN_SPACE, ';');
        break;

    }

    rplSetSystemNumberFormat(&fmt);
    uiClearRenderCache();
    halScreen.DirtyFlag |= STACK_DIRTY;

}

void onSpcKeyHandler(WORD keymsg)
{
    UNUSED(keymsg);

    // CYCLE BETWEEN VARIOUS OPTIONS
    const char *const options[] = {
        "STD", "FIX", "SCI", "ENG"
    };

    NUMFORMAT fmt;
    int32_t option = 0;
    rplGetSystemNumberFormat(&fmt);

    if(fmt.MiddleFmt & FMT_TRAILINGZEROS)
        option = 1;
    if(fmt.MiddleFmt & FMT_SCI)
        option = 2;
    if(fmt.MiddleFmt & FMT_ENG)
        option = 3;

    // CYCLE THROUGH ITEMS:
    ++option;
    if(option > 3)
        option = 0;

    halStatusAreaPopup();

    gglsurface scr;
    ggl_initscr(&scr);
    int ytop =
            halScreen.Form + halScreen.Stack + halScreen.CmdLine +
            halScreen.Menu1;
    // CLEAR STATUS AREA
    ggl_rect(&scr, STATUS_AREA_X, ytop, LCD_W - 1,
            ytop + halScreen.Menu2 - 1, ggl_color(PAL_STA_BG));

    DrawTextBk(STATUS_AREA_X + 1, ytop + 1, "Display Mode:",
               FONT_STATUS, ggl_color(PAL_STA_TEXT), ggl_color(PAL_STA_BG), &scr);
    DrawTextBk(STATUS_AREA_X + 1,
               ytop + 1 + FONT_HEIGHT(FONT_STATUS),
               (char *)options[option], FONT_STATUS, ggl_color(PAL_STA_TEXT),ggl_color(PAL_STA_BG),
               &scr);

    // CHANGE THE FORMAT TO THE SELECTED OPTION
    switch (option) {
    default:
    case 0:
        fmt.MiddleFmt &= FMT_NUMSEPARATOR | FMT_FRACSEPARATOR | FMT_GROUPDIGITSMSK | FMT_USECAPITALS | FMT_NUMDIGITS | FMT_PREFEXPMSK;  // PRESERVE ALL THESE
        fmt.BigFmt &= FMT_SCI | FMT_NUMSEPARATOR | FMT_FRACSEPARATOR | FMT_GROUPDIGITSMSK | FMT_USECAPITALS | FMT_NUMDIGITS | FMT_PREFEXPMSK;  // PRESERVE ALL THESE
        fmt.SmallFmt &= FMT_SCI | FMT_NUMSEPARATOR | FMT_FRACSEPARATOR | FMT_GROUPDIGITSMSK | FMT_USECAPITALS | FMT_NUMDIGITS | FMT_PREFEXPMSK;  // PRESERVE ALL THESE
        break;
    case 1:
        fmt.MiddleFmt &= FMT_NUMSEPARATOR | FMT_FRACSEPARATOR | FMT_GROUPDIGITSMSK | FMT_USECAPITALS | FMT_NUMDIGITS | FMT_PREFEXPMSK;  // PRESERVE ALL THESE
        fmt.BigFmt &= FMT_SCI | FMT_NUMSEPARATOR | FMT_FRACSEPARATOR | FMT_GROUPDIGITSMSK | FMT_USECAPITALS | FMT_NUMDIGITS | FMT_PREFEXPMSK;  // PRESERVE ALL THESE
        fmt.SmallFmt &= FMT_SCI |FMT_NUMSEPARATOR | FMT_FRACSEPARATOR | FMT_GROUPDIGITSMSK | FMT_USECAPITALS | FMT_NUMDIGITS | FMT_PREFEXPMSK;  // PRESERVE ALL THESE

        fmt.MiddleFmt |= FMT_TRAILINGZEROS;
        fmt.BigFmt |=  FMT_TRAILINGZEROS;
        fmt.SmallFmt |= FMT_TRAILINGZEROS;

        break;
    case 2:
        fmt.MiddleFmt &= FMT_NUMSEPARATOR | FMT_FRACSEPARATOR | FMT_GROUPDIGITSMSK | FMT_USECAPITALS | FMT_NUMDIGITS | FMT_PREFEXPMSK;  // PRESERVE ALL THESE
        fmt.BigFmt &= FMT_SCI | FMT_NUMSEPARATOR | FMT_FRACSEPARATOR | FMT_GROUPDIGITSMSK | FMT_USECAPITALS | FMT_NUMDIGITS | FMT_PREFEXPMSK;  // PRESERVE ALL THESE
        fmt.SmallFmt &= FMT_SCI |FMT_NUMSEPARATOR | FMT_FRACSEPARATOR | FMT_GROUPDIGITSMSK | FMT_USECAPITALS | FMT_NUMDIGITS | FMT_PREFEXPMSK;  // PRESERVE ALL THESE

        fmt.MiddleFmt |= FMT_SCI;
        fmt.BigFmt |= FMT_SCI;
        fmt.SmallFmt |= FMT_SCI;
        break;
    case 3:
        fmt.MiddleFmt &= FMT_NUMSEPARATOR | FMT_FRACSEPARATOR | FMT_GROUPDIGITSMSK | FMT_USECAPITALS | FMT_NUMDIGITS | FMT_PREFEXPMSK;  // PRESERVE ALL THESE
        fmt.BigFmt &= FMT_SCI | FMT_NUMSEPARATOR | FMT_FRACSEPARATOR | FMT_GROUPDIGITSMSK | FMT_USECAPITALS | FMT_NUMDIGITS | FMT_PREFEXPMSK;  // PRESERVE ALL THESE
        fmt.SmallFmt &= FMT_SCI |FMT_NUMSEPARATOR | FMT_FRACSEPARATOR | FMT_GROUPDIGITSMSK | FMT_USECAPITALS | FMT_NUMDIGITS | FMT_PREFEXPMSK;  // PRESERVE ALL THESE

        fmt.MiddleFmt |= FMT_SCI | FMT_ENG;
        fmt.BigFmt |= FMT_SCI | FMT_ENG;
        fmt.SmallFmt |= FMT_SCI | FMT_ENG;
        if((PREFERRED_EXPRAW(fmt.MiddleFmt) == 0)
                || (PREFERRED_EXPRAW(fmt.MiddleFmt) == 8))
        {
            fmt.MiddleFmt |= FMT_SUPRESSEXP;    // SUPRESS ZERO EXPONENT ONLY WHEN FIXED EXPONENT IS ZERO
            fmt.BigFmt |= FMT_SUPRESSEXP;    // SUPRESS ZERO EXPONENT ONLY WHEN FIXED EXPONENT IS ZERO
            fmt.SmallFmt |= FMT_SUPRESSEXP;    // SUPRESS ZERO EXPONENT ONLY WHEN FIXED EXPONENT IS ZERO
        }

        break;
    }

    rplSetSystemNumberFormat(&fmt);
    uiClearRenderCache();
    halScreen.DirtyFlag |= STACK_DIRTY;

}

const const char *const onMulDivKeyHandler_options[] = { "Auto",    "  =  0",  "k = +3",  "M = +6",
                                                         "G = +9",  "T = +12", "P = +15", "E = +18",
                                                         "Z = +21", "z = -21", "a = -18", "f = -15",
                                                         "p = -12", "n = -9",  "µ = -6",  "m = -3" };

void onMulDivKeyHandler(WORD keymsg)
{

    // CYCLE BETWEEN VARIOUS OPTIONS

    NUMFORMAT fmt;
    int32_t option = 0;
    rplGetSystemNumberFormat(&fmt);

    option = PREFERRED_EXPRAW(fmt.MiddleFmt);
    if(option) {
        option -= 7;
        if(option <= 0)
            option += 15;
    }

    // CYCLE THROUGH ITEMS:
    if(KM_KEY(keymsg) == KB_MUL) {
        if(option != 9)
            --option;
    }
    else {
        if(option != 8)
            ++option;
    }
    if(option < 0)
        option = 15;
    if(option > 15)
        option = 0;

    halStatusAreaPopup();

    gglsurface scr;
    ggl_initscr(&scr);
    int ytop =
            halScreen.Form + halScreen.Stack + halScreen.CmdLine +
            halScreen.Menu1;
    // CLEAR STATUS AREA
    ggl_rect(&scr, STATUS_AREA_X, ytop, LCD_W - 1,
            ytop + halScreen.Menu2 - 1, ggl_color(PAL_STA_BG));

    DrawTextBk(STATUS_AREA_X + 1, ytop + 1, "ENG exponent:",
               FONT_STATUS, ggl_color(PAL_STA_TEXT), ggl_color(PAL_STA_BG), &scr);
    DrawTextBk(STATUS_AREA_X + 1,
               ytop + 1 + FONT_HEIGHT(FONT_STATUS),
               (char *)onMulDivKeyHandler_options[option],
               FONT_STATUS, ggl_color(PAL_STA_TEXT), ggl_color(PAL_STA_BG), &scr);

    if(option)
        option += 7;
    if(option > 15)
        option -= 15;

    fmt.MiddleFmt &= FMT_NUMSEPARATOR | FMT_FRACSEPARATOR | FMT_GROUPDIGITSMSK | FMT_USECAPITALS | FMT_NUMDIGITS;       // PRESERVE ALL THESE
    fmt.MiddleFmt |= FMT_SCI | FMT_ENG | FMT_PREFEREXPRAW(option);
    fmt.BigFmt &= FMT_NUMSEPARATOR | FMT_FRACSEPARATOR | FMT_GROUPDIGITSMSK | FMT_USECAPITALS | FMT_NUMDIGITS;       // PRESERVE ALL THESE
    fmt.BigFmt |= FMT_SCI | FMT_ENG | FMT_PREFEREXPRAW(option);
    fmt.SmallFmt &= FMT_NUMSEPARATOR | FMT_FRACSEPARATOR | FMT_GROUPDIGITSMSK | FMT_USECAPITALS | FMT_NUMDIGITS;       // PRESERVE ALL THESE
    fmt.SmallFmt |= FMT_SCI | FMT_ENG | FMT_PREFEREXPRAW(option);
    if((option == 0) || (option == 8))
    {
        fmt.MiddleFmt |= FMT_SUPRESSEXP;        // SUPRESS ZERO EXPONENT ONLY WHEN FIXED EXPONENT IS ZERO
        fmt.BigFmt |= FMT_SUPRESSEXP;        // SUPRESS ZERO EXPONENT ONLY WHEN FIXED EXPONENT IS ZERO
        fmt.SmallFmt |= FMT_SUPRESSEXP;        // SUPRESS ZERO EXPONENT ONLY WHEN FIXED EXPONENT IS ZERO
    }

    rplSetSystemNumberFormat(&fmt);
    uiClearRenderCache();
    halScreen.DirtyFlag |= STACK_DIRTY | MENU1_DIRTY | MENU2_DIRTY;

}

void onDigitKeyHandler(WORD keymsg)
{
    NUMFORMAT fmt;
    int32_t digits = 0;
    rplGetSystemNumberFormat(&fmt);

    switch (KM_KEY(keymsg)) {
    case KB_0:
        digits = 0xfff;
        break;
    case KB_1:
        digits = 1;
        break;
    case KB_2:
        digits = 2;
        break;
    case KB_3:
        digits = 3;
        break;
    case KB_4:
        digits = 4;
        break;
    case KB_5:
        digits = 5;
        break;
    case KB_6:
        digits = 6;
        break;
    case KB_7:
        digits = 7;
        break;
    case KB_8:
        digits = 8;
        break;
    case KB_9:
        digits = 9;
        break;
    }

    fmt.MiddleFmt &= ~FMT_NUMDIGITS;
    fmt.MiddleFmt |= FMT_DIGITS(digits);


    fmt.BigFmt &= ~FMT_NUMDIGITS;
    fmt.BigFmt |= FMT_DIGITS(digits);
    fmt.SmallFmt &= ~FMT_NUMDIGITS;
    fmt.SmallFmt |= FMT_DIGITS(digits);



    fmt.SmallLimit.data=fmt.SmallLimitData;
    newRealFromint32_t(&fmt.SmallLimit,1,(digits==0xfff)? -12:-digits);



    if(digits==0xfff) digits= TEXT2WORD('A','l','l',0);
      else digits += '0';

    halStatusAreaPopup();

    gglsurface scr;
    ggl_initscr(&scr);
    int ytop =
            halScreen.Form + halScreen.Stack + halScreen.CmdLine +
            halScreen.Menu1;
    // CLEAR STATUS AREA
    ggl_rect(&scr, STATUS_AREA_X, ytop, LCD_W - 1,
            ytop + halScreen.Menu2 - 1, ggl_color(PAL_STA_BG));

    DrawTextBk(STATUS_AREA_X + 1, ytop + 1, "Display Digits:",
               FONT_STATUS, ggl_color(PAL_STA_TEXT), ggl_color(PAL_STA_BG), &scr);
    DrawTextBk(STATUS_AREA_X + 1,
               ytop + 1 + FONT_HEIGHT(FONT_STATUS),
               (char *)&digits, FONT_STATUS, ggl_color(PAL_STA_TEXT), ggl_color(PAL_STA_BG), &scr);

    rplSetSystemNumberFormat(&fmt);
    uiClearRenderCache();
    halScreen.DirtyFlag |= STACK_DIRTY | MENU1_DIRTY | MENU2_DIRTY;

}

void onUpDownKeyHandler(WORD keymsg)
{
    int32_t precision = Context.precdigits;

    if(KM_KEY(keymsg) == KB_UP)
        precision += 8;
    else
        precision -= 8;

    if(precision < 8)
        precision = 8;
    if(precision > MAX_USERPRECISION)
        precision = MAX_USERPRECISION;

    Context.precdigits = precision;

    char digits_string[12], empty = ' ';

    stringcpy(digits_string, "0000 digits");

    if(precision >= 1000) {
        digits_string[0] = precision / 1000 + '0';
        precision = precision % 1000;
        empty = '0';
    }
    else
        digits_string[0] = empty;
    if(precision >= 100) {
        digits_string[1] = precision / 100 + '0';
        precision = precision % 100;
        empty = '0';
    }
    else
        digits_string[1] = empty;
    if(precision >= 10) {
        digits_string[2] = precision / 10 + '0';
        precision = precision % 10;
        empty = '0';
    }
    else
        digits_string[2] = empty;
    digits_string[3] = precision + '0';

    halStatusAreaPopup();

    gglsurface scr;
    ggl_initscr(&scr);
    int ytop =
            halScreen.Form + halScreen.Stack + halScreen.CmdLine +
            halScreen.Menu1;
    // CLEAR STATUS AREA
    ggl_rect(&scr, STATUS_AREA_X, ytop, LCD_W - 1,
            ytop + halScreen.Menu2 - 1, ggl_color(PAL_STA_BG));

    DrawTextBk(STATUS_AREA_X + 1, ytop + 1, "System precision:",
               FONT_STATUS, ggl_color(PAL_STA_TEXT),ggl_color(PAL_STA_BG), &scr);
    DrawTextBk(STATUS_AREA_X + 1,
               ytop + 1 + FONT_HEIGHT(FONT_STATUS),
               digits_string, FONT_STATUS, ggl_color(PAL_STA_TEXT), ggl_color(PAL_STA_BG), &scr);

    halScreen.DirtyFlag |= STACK_DIRTY;

}

// SHOW/HIDE THE SECOND MENU WHEN PRESSED
void onVarKeyHandler(WORD keymsg)
{
    UNUSED(keymsg);
    if(halScreen.Menu2) {
        halSetMenu2Height(0);   // HIDE THE MENU
        rplSetSystemFlag(FL_HIDEMENU2);
    }
    else {
        halSetMenu2Height(MENU2_HEIGHT);
        rplClrSystemFlag(FL_HIDEMENU2);
    }

}

void onBKeyHandler(WORD keymsg)
{
    UNUSED(keymsg);

    halFlags |= HAL_SKIPNEXTALARM;

}

void alphaKeyHandler(WORD keymsg)
{
    UNUSED(keymsg);

    if((halScreen.CursorState & 0xff) == 'L') {
        halSetCmdLineMode('C');
        halScreen.DirtyFlag |= CMDLINE_CURSORDIRTY;

    }
    else {
        if((halScreen.CursorState & 0xff) == 'C') {
            halSetCmdLineMode('L');
            halScreen.DirtyFlag |= CMDLINE_CURSORDIRTY;

        }

    }
}

void shiftedalphaKeyHandler(WORD keymsg)
{
    // CYCLE BETWEEN D, P AND A MODES WHEN ALPHA IS DISABLED
    UNUSED(keymsg);

    switch (halScreen.CursorState & 0xff) {
    case 'D':
        halSetCmdLineMode('P');
        halScreen.DirtyFlag |= CMDLINE_CURSORDIRTY;
        break;
    case 'P':
        halSetCmdLineMode('A');
        halScreen.DirtyFlag |= CMDLINE_CURSORDIRTY;
        break;
    case 'A':
        halSetCmdLineMode('P');
        halScreen.DirtyFlag |= CMDLINE_CURSORDIRTY;
        break;
    }

}

void changemenuKeyHandler(WORD keymsg, int64_t menucode)
{
    UNUSED(keymsg);

    word_p numobject = rplNewint32_t(menucode, HEXint32_t);

    if(!numobject || Exceptions)
        return;
    int32_t menu = rplGetActiveMenu();

    rplPushDataNoGrow(numobject);
    rplSaveMenuHistory(menu);
    rplChangeMenu(menu, rplPopData());

    if(menu == 1)
        halScreen.DirtyFlag |= MENU1_DIRTY;
    else
        halScreen.DirtyFlag |= MENU2_DIRTY;

}

void backmenuKeyHandler(WORD keymsg, WORD menu)
{
    UNUSED(keymsg);

    if(menu < 1 || menu > 2)
        return;

    word_p oldmenu = rplPopMenuHistory(menu);
    if(oldmenu) {
        rplChangeMenu(menu, oldmenu);

        if(menu == 1)
            halScreen.DirtyFlag |= MENU1_DIRTY;
        else
            halScreen.DirtyFlag |= MENU2_DIRTY;
    }

}

void backmenu1KeyHandler(WORD keymsg)
{
    backmenuKeyHandler(keymsg, 1);
}

void backmenu2KeyHandler(WORD keymsg)
{
    backmenuKeyHandler(keymsg, 2);
}

// CUSTOM KEY DEFINITIONS - LOWER LEVEL HANDLER
void customKeyHandler(WORD keymsg, word_p action)
{
    if(!action)
        return;
    int32_t inlist = 0;
    // COMMANDS CAN BE PUT INSIDE LISTS
    if(ISLIST(*action)) {
        action = rplGetListElement(action, 1);
        if(!action)
            return;
        if(*action == CMD_ENDLIST)
            return;     // EMPTY LIST!
        inlist = 1;
    }

    // DEFAULT MESSAGE
    if(!(halGetContext() & CONTEXT_INEDITOR)) {
        // ACTION WHEN IN THE STACK OR SUBCONTEXTS OTHER THAN THE EDITOR
        WORD Opcode = 0;
        int32_t hideargument = 1;

        // DO DIFFERENT ACTIONS BASED ON OBJECT TYPE

        if(ISIDENT(*action)) {
            // JUST EVAL THE VARIABLE
            rplPushData(action);        // PUSH THE NAME ON THE STACK
            Opcode = (CMD_OVR_EVAL1);
        }
        else if((!ISPROLOG(*action)) && (!ISint32_t(*action))) {
            // THIS IS AN OPCODE, EXECUTE DIRECTLY
            Opcode = *action;
            hideargument = 0;
        }
        else if(ISSTRING(*action) && inlist) {
            // A STRING TO INSERT INTO THE EDITOR
            // OPEN AN EDITOR AND INSERT THE STRING
            halSetCmdLineHeight(CMDLINE_HEIGHT);
            halSetContext(halGetContext() | CONTEXT_INEDITOR);
            if(KM_SHIFTPLANE(keymsg) & SHIFT_ALPHA)
                uiOpenCmdLine('X');
            else
                uiOpenCmdLine('D');

            int32_t nlines =
                    uiInsertCharactersN((byte_p) (action + 1),
                    (byte_p) (action + 1) + rplStrSize(action));
            if(nlines)
                uiStretchCmdLine(nlines);

            uiAutocompleteUpdate();

        }
        else {
            // ALL OTHER OBJECTS AND COMMANDS, DO XEQ
            rplPushData(action);
            Opcode = (CMD_OVR_XEQ);
        }

        if(Opcode)
            uiCmdRunHide(Opcode, hideargument);
        if(Exceptions) {
            // TODO: SHOW ERROR MESSAGE
            halShowErrorMsg();
            Exceptions = 0;
        }
        else
            halScreen.DirtyFlag |= MENU1_DIRTY | MENU2_DIRTY;
        halScreen.DirtyFlag |= STACK_DIRTY | STAREA_DIRTY;

    }
    else {
        // ACTION INSIDE THE EDITOR
        WORD Opcode = 0;
        int32_t hideargument = 1;

        if(!action)
            return;

        // DO DIFFERENT ACTIONS BASED ON OBJECT TYPE

        if(ISIDENT(*action) && inlist) {
            switch (halScreen.CursorState & 0xff) {
            case 'D':
            {
                // HANDLE DIRECTORIES IN A SPECIAL WAY: DON'T CLOSE THE COMMAND LINE
                word_p *var = rplFindGlobal(action, 1);
                if(var) {
                    if(ISDIR(*(var[1]))) {
                        // CHANGE THE DIR WITHOUT CLOSING THE COMMAND LINE
                        rplPushData(action);    // PUSH THE NAME ON THE STACK
                        Opcode = (CMD_OVR_EVAL);
                        break;
                    }
                }
                rplPushRet(action);
                int32_t result = endCmdLineAndCompile();
                action = rplPopRet();
                if(result) {
                    // USER IS TRYING TO EVAL THE VARIABLE
                    rplPushData(action);        // PUSH THE NAME ON THE STACK
                    Opcode = (CMD_OVR_EVAL);
                }
                break;
            }
            case 'A':
            {
                word_p *var = rplFindGlobal(action, 1);
                if(var) {
                    if(ISDIR(*(var[1]))) {
                        // CHANGE THE DIR WITHOUT CLOSING THE COMMAND LINE
                        rplPushData(action);    // PUSH THE NAME ON THE STACK
                        Opcode = (CMD_OVR_EVAL);
                        break;
                    }
                }

                byte_p string, endstring;
                if (!rplGetDecompiledStringWithoutTickmarks(action, DECOMP_EDIT, &string, &endstring))
                    break; // ERROR WITHIN A MENU PROGRAM! JUST IGNORE FOR NOW

                uiInsertCharactersN(string, endstring);
                uiAutocompleteUpdate();

                break;
            }

            case 'P':
            {
                word_p *var = rplFindGlobal(action, 1);
                if(var) {
                    if(ISDIR(*(var[1]))) {
                        // CHANGE THE DIR WITHOUT CLOSING THE COMMAND LINE
                        rplPushData(action);    // PUSH THE NAME ON THE STACK
                        Opcode = (CMD_OVR_EVAL);
                        break;
                    }
                }

                byte_p string, endstring;
                if (!rplGetDecompiledStringWithoutTickmarks(action, DECOMP_EDIT, &string, &endstring))
                    break; // ERROR WITHIN A MENU PROGRAM! JUST IGNORE FOR NOW

                uiSeparateToken();
                uiInsertCharactersN(string, endstring);

                uiSeparateToken();
                uiAutocompleteUpdate();

                break;
            }
            }
        }
        else if(ISUNIT(*action)) {
            switch (halScreen.CursorState & 0xff) {
            case 'D':
            {
                rplPushRet(action);
                int32_t result = endCmdLineAndCompile();
                action = rplPopRet();
                if(result) {
                    // USER IS TRYING TO APPLY THE UNIT
                    rplPushData(action);        // PUSH THE NAME ON THE STACK
                    Opcode = (CMD_OVR_MUL);
                }
                break;
            }
            case 'A':
            {
                byte_p string, endstring;
                int32_t totaln = rplGetDecompiledString(action, DECOMP_EDIT, &string, &endstring);
                if (!totaln)
                    break; // ERROR WITHIN A MENU PROGRAM! JUST IGNORE FOR NOW

                if((totaln > 2) && (string[0] == '1') && (string[1] == '_'))
                    string += 2;

                uiInsertCharactersN(string, endstring);

                uiAutocompleteUpdate();

                break;
            }

            case 'P':
            {
                byte_p string, endstring;
                if (!rplGetDecompiledString(action, DECOMP_EDIT, &string, &endstring))
                    break; // ERROR WITHIN A MENU PROGRAM! JUST IGNORE FOR NOW

                uiSeparateToken();
                uiInsertCharactersN(string, endstring);
                uiSeparateToken();
                uiInsertCharacters((byte_p) "*");
                uiSeparateToken();
                uiAutocompleteUpdate();

                break;
            }

            }
        }
        else if(!ISPROLOG(*action)) {
            // THIS IS A COMMAND, DECOMPILE AND INSERT NAME
            switch (halScreen.CursorState & 0xff) {
            case 'D':
            {
                rplPushRet(action);
                int32_t result = endCmdLineAndCompile();
                action = rplPopRet();
                if(result) {
                    Opcode = *action;
                    hideargument = 0;
                }
                break;
            }
            case 'A':
            {

                WORD tokeninfo = 0;
                LIBHANDLER han = rplGetLibHandler(LIBNUM(*action));

                // GET THE SYMBOLIC TOKEN INFORMATION
                if(han) {
                    WORD savecurOpcode = CurOpcode;
                    DecompileObject = action;
                    CurOpcode = MKOPCODE(LIBNUM(*action), OPCODE_GETINFO);
                    (*han) ();

                    if(RetNum > OK_TOKENINFO)
                        tokeninfo = RetNum;

                    CurOpcode = savecurOpcode;
                }
                byte_p string, endstring;
                if (!rplGetDecompiledString(action, DECOMP_EDIT | DECOMP_NOHINTS, &string, &endstring))
                    break; // ERROR WITHIN A MENU PROGRAM! JUST IGNORE FOR NOW

                uiInsertCharactersN(string, endstring);
                if(TI_TYPE(tokeninfo) == TITYPE_FUNCTION) {
                    uiInsertCharacters((byte_p) "()");
                    uiCursorLeft(1);
                }
                uiAutocompleteUpdate();

                break;
            }

            case 'P':
            {
                byte_p string, endstring;
                if (!rplGetDecompiledString(action, DECOMP_EDIT, &string, &endstring))
                    break; // ERROR WITHIN A MENU PROGRAM! JUST IGNORE FOR NOW

                uiSeparateToken();
                int32_t nlines = uiInsertCharactersN(string, endstring);
                if(nlines)
                    uiStretchCmdLine(nlines);

                uiSeparateToken();
                uiAutocompleteUpdate();

                break;
            }

            }

        }
        else if(ISPROGRAM(*action)) {
            if(!ISSECO(*action)) {
                // IT'S A DOCOL PROGRAM, EXECUTE TRANSPARENTLY
                rplPushData(action);    // PUSH THE NAME ON THE STACK
                Opcode = CMD_OVR_XEQ;
            }
            else {
                rplPushRet(action);
                int32_t result = endCmdLineAndCompile();
                action = rplPopRet();
                if(result) {
                    rplPushData(action);        // PUSH THE NAME ON THE STACK
                    Opcode = CMD_OVR_XEQ;
                }
            }
        }
        else if(ISSTRING(*action)) {
            byte_p string, endstring;
            rplGetStringPointers(action, &string, &endstring);

            if(!inlist) {
                // ADD THE QUOTES IN D OR P MODE
                if(((halScreen.CursorState & 0xff) == 'P')
                        || ((halScreen.CursorState & 0xff) == 'D')) {
                    uiSeparateToken();
                    uiInsertCharacters((byte_p) "\"");
                }
            }
            uiInsertCharactersN(string, endstring);
            if(!inlist && (((halScreen.CursorState & 0xff) == 'P')
                        || ((halScreen.CursorState & 0xff) == 'D'))) {
                uiInsertCharacters((byte_p) "\"");
                uiSeparateToken();
            }
            uiAutocompleteUpdate();
        }
        else {
            // ALL OTHER OBJECTS AND COMMANDS
            switch (halScreen.CursorState & 0xff) {
            case 'D':
            {
                rplPushRet(action);
                int32_t result = endCmdLineAndCompile();
                action = rplPopRet();
                if(result) {
                    if(!ISPROLOG(*action)) {
                        Opcode = *action;       // RUN COMMANDS DIRECTLY
                        hideargument = 0;
                    }
                    else {
                        Opcode = (CMD_OVR_XEQ);
                        rplPushData(action);
                    }
                }
                break;
            }
            case 'A':
            {

                WORD tokeninfo = 0;
                LIBHANDLER han = rplGetLibHandler(LIBNUM(*action));

                // GET THE SYMBOLIC TOKEN INFORMATION
                if(han) {
                    WORD savecurOpcode = CurOpcode;
                    DecompileObject = action;
                    CurOpcode = MKOPCODE(LIBNUM(*action), OPCODE_GETINFO);
                    (*han) ();

                    if(RetNum > OK_TOKENINFO)
                        tokeninfo = RetNum;

                    CurOpcode = savecurOpcode;
                }
                byte_p string, endstring;
                if (!rplGetDecompiledString(action, DECOMP_EDIT | DECOMP_NOHINTS, &string, &endstring))
                    break; // ERROR WITHIN A MENU PROGRAM! JUST IGNORE FOR NOW

                uiInsertCharactersN(string, endstring);
                if(TI_TYPE(tokeninfo) == TITYPE_FUNCTION) {
                    uiInsertCharacters((byte_p) "()");
                    uiCursorLeft(1);
                }
                uiAutocompleteUpdate();

                break;
            }

            case 'P':
            {
                byte_p string, endstring;
                if (!rplGetDecompiledString(action, DECOMP_EDIT, &string, &endstring))
                    break; // ERROR WITHIN A MENU PROGRAM! JUST IGNORE FOR NOW

                uiSeparateToken();
                int32_t nlines = uiInsertCharactersN(string, endstring);
                if(nlines)
                    uiStretchCmdLine(nlines);

                uiSeparateToken();
                uiAutocompleteUpdate();

                break;
            }

            }

        }

        if(Opcode)
            uiCmdRunHide(Opcode, hideargument);
        if(Exceptions) {
            // TODO: SHOW ERROR MESSAGE
            halShowErrorMsg();
            Exceptions = 0;
        }
        else
            halScreen.DirtyFlag |= MENU1_DIRTY | MENU2_DIRTY;
        halScreen.DirtyFlag |= STACK_DIRTY | STAREA_DIRTY;
    }

}

void formswitcherKeyHandler(WORD keymsg)
{
    UNUSED(keymsg);

    if(halGetContext() >= CONTEXT_FORM) {
        // THE USER IS RUNNING A FORM OR OTHER CONTEXT, JUST CLOSE IT

        //uiCloseFormEvent();
        halSwitch2Stack();
        return;
    }
    halSwitch2Form();
    return;
}

#ifdef TARGET_PRIME1
void switchmenukeysKeyHandler(WORD keymsg)
{
    UNUSED(keymsg);


    // TODO: Just toggle a flag that the key handlers read and act on menu 1 or 2
    halKeyMenuSwitch^=1;

    halScreen.DirtyFlag |= MENU1_DIRTY | MENU2_DIRTY;

    return;
}
#endif /* TARGET_PRIME1 */


void basecycleKeyHandler(WORD keymsg)
{
    UNUSED(keymsg);

    if(!(halGetContext() & CONTEXT_INEDITOR)) {
        if(halGetContext() & CONTEXT_STACK) {
            // ACTION WHEN IN THE STACK, CYCLE THROUGH DIFFERENT BASES

            rplPushDataNoGrow((word_p) lib70_basecycle);
            uiCmdRunHide(CMD_OVR_XEQ, 1);
            if(Exceptions) {
                // TODO: SHOW ERROR MESSAGE
                halShowErrorMsg();
                Exceptions = 0;
            }
            else
                halScreen.DirtyFlag |= MENU1_DIRTY | MENU2_DIRTY | STAREA_DIRTY;
            halScreen.DirtyFlag |= STACK_DIRTY;

        }

    }
    else {

        // ACTION INSIDE THE EDITOR

        byte_p startnum, endnum;
        byte_p line;
        int32_t numflags;

        // FIRST CASE: IF TOKEN UNDER THE CURSOR IS OR CONTAINS A VALID NUMBER, CHANGE THE BASE OF THE NUMBER IN THE TEXT
        startnum = uiFindNumberStart(&endnum, &numflags);
        line = (byte_p) (CmdLineCurrentLine + 1);
        if(!startnum) {

            return;

        }
        else {
            // WE FOUND A NUMBER
            int32_t oldposition = halScreen.CursorPosition;
            uiMoveCursor(startnum - line);
            BYTE str[2];
            int32_t endchar, minbase = numflags & 0xffff;

            numflags >>= 16;
            str[1] = 0;
            str[0] = '#';

            if(endnum > startnum)
                endchar = *endnum;
            else
                endchar = 0;

            // TODO: FIND THE END OF THE TOKEN, AND CYCLE THE LETTER
            switch (endchar) {
            case 'b':
            case 'B':
                endchar = 'o';
                break;
            case 'o':
            case 'O':
                endchar = 'h';
                break;
            case 'h':
            case 'H':
                endchar = -1;
                break;
            default:
                endchar = 0;
            }

            if(endchar < 0) {
                if(minbase <= 10) {
                    // REMOVE NUMERAL SIGN
                    if(startnum[0] == '#') {
                        uiRemoveCharacters(1);
                        if(oldposition > startnum - line)
                            --oldposition;
                        --endnum;
                    }
                }
                else if(minbase == 2)
                    endchar = 'b';
                else if(minbase == 8)
                    endchar = 'o';
                else
                    endchar = 'h';
            }
            else {
                // ADD NUMERAL SIGN IF NOT THERE YET AND IF THERE'S NO EXPONENT WITH A SIGN
                if(!(numflags & 8) && (startnum[0] != '#')) {
                    uiInsertCharacters((byte_p) str);
                    if(oldposition > startnum - line)
                        ++oldposition;
                    ++endnum;
                }
            }

            if(endchar != 0) {
                uiMoveCursor(endnum - line);
                uiRemoveCharacters(1);
                if(oldposition > endnum - line)
                    --oldposition;
            }
            else {
                uiMoveCursor(endnum - line + 1);
                if(minbase == 2)
                    endchar = 'b';
                else if(minbase == 8)
                    endchar = 'o';
                else
                    endchar = 'h';
            }
            if((endchar > 0) && !(numflags & 8))        // IF WE NEED TO SET A BASE AND THERE'S NO EXPONENT SIGN IN BETWEEN
            {

                str[0] = endchar;
                uiInsertCharacters((byte_p) str);
                if(oldposition >= endnum - line)
                    ++oldposition;
            }

            uiMoveCursor(oldposition);
            uiEnsureCursorVisible();
            uiAutocompleteUpdate();
            return;
        }

        // THIRD CASE: IF TOKEN UNDER CURSOR IS SOMETHING OTHER THAN A NUMBER, JUST DO NOTHING

    }
}

#define DECLARE_TRANSPCMDKEYHANDLER(name,opcode) void name##KeyHandler(WORD keymsg) \
{ \
    UNUSED(keymsg); \
    transpcmdKeyHandler(opcode); \
    }

#define DECLARE_CMDKEYHANDLER(name,opcode,string,issymbfunc) void name##KeyHandler(WORD keymsg) \
{ \
    UNUSED(keymsg); \
    cmdKeyHandler(opcode,(byte_p)string,issymbfunc); \
    }

#define DECLARE_VARKEYHANDLER(name,menu,idx) void name##KeyHandler(WORD keymsg) \
{ \
    varsKeyHandler(keymsg,(menu),(int32_t)(idx)); \
    }

#define DECLARE_MENUKEYHANDLER(name,menucode) void name##KeyHandler(WORD keymsg) \
{ \
    changemenuKeyHandler(keymsg,(int64_t)(menucode)); \
    }

#define DECLARE_KEYHANDLER(name,lsymbol,csymbol) void name##KeyHandler(WORD keymsg) \
{ \
    alphasymbolKeyHandler(keymsg,(byte_p)(lsymbol),(byte_p)(csymbol)); \
    }

#define DECLARE_SYMBKEYHANDLER(name,symbol,sep) void name##KeyHandler(WORD keymsg) \
{ \
    symbolKeyHandler(keymsg,(byte_p)(symbol),(sep)); \
    }

#define KEYHANDLER_NAME(name)  &(name##KeyHandler)

DECLARE_KEYHANDLER(a, "a", "A")
        DECLARE_KEYHANDLER(b, "b", "B")
        DECLARE_KEYHANDLER(c, "c", "C")
        DECLARE_KEYHANDLER(d, "d", "D")
        DECLARE_KEYHANDLER(e, "e", "E")
        DECLARE_KEYHANDLER(f, "f", "F")
        DECLARE_KEYHANDLER(g, "g", "G")
        DECLARE_KEYHANDLER(h, "h", "H")
        DECLARE_KEYHANDLER(i, "i", "I")
        DECLARE_KEYHANDLER(j, "j", "J")
        DECLARE_KEYHANDLER(k, "k", "K")
        DECLARE_KEYHANDLER(l, "l", "L")
        DECLARE_KEYHANDLER(m, "m", "M")
        DECLARE_KEYHANDLER(n, "n", "N")
        DECLARE_KEYHANDLER(o, "o", "O")
        DECLARE_KEYHANDLER(p, "p", "P")
        DECLARE_KEYHANDLER(q, "q", "Q")
        DECLARE_KEYHANDLER(r, "r", "R")
        DECLARE_KEYHANDLER(s, "s", "S")
        DECLARE_KEYHANDLER(t, "t", "T")
        DECLARE_KEYHANDLER(u, "u", "U")
        DECLARE_KEYHANDLER(v, "v", "V")
        DECLARE_KEYHANDLER(w, "w", "W")
        DECLARE_KEYHANDLER(x, "x", "X")
        DECLARE_KEYHANDLER(y, "y", "Y")
        DECLARE_KEYHANDLER(z, "z", "Z")
        DECLARE_SYMBKEYHANDLER(arrow, "→", 1)
        DECLARE_SYMBKEYHANDLER(rulesep, ":→", 1)
        DECLARE_SYMBKEYHANDLER(comma, ",", 0)
        DECLARE_SYMBKEYHANDLER(semi, ";", 0)
        DECLARE_SYMBKEYHANDLER(colon, ":", 0)
        DECLARE_SYMBKEYHANDLER(infinity, "∞", 1)
        DECLARE_SYMBKEYHANDLER(undinfinity, "∞̅", 1)
        DECLARE_SYMBKEYHANDLER(dot, ".", 0)
        DECLARE_SYMBKEYHANDLER(giventhat, "|", 0)
        DECLARE_SYMBKEYHANDLER(question, "?", 0)
        DECLARE_SYMBKEYHANDLER(openquestion, "¿", 0)
        DECLARE_SYMBKEYHANDLER(exclamation, "!", 0)
        DECLARE_SYMBKEYHANDLER(openexclamation, "¡", 0)
        DECLARE_SYMBKEYHANDLER(approx, "~", 0)
        DECLARE_SYMBKEYHANDLER(percent, "%", 0)
        DECLARE_SYMBKEYHANDLER(dollar, "$", 0)
        DECLARE_SYMBKEYHANDLER(euro, "€", 0)
        DECLARE_SYMBKEYHANDLER(backslash, "\\", 0)
        DECLARE_SYMBKEYHANDLER(pound, "£", 0)
        DECLARE_SYMBKEYHANDLER(angle, "∡", 0)
        DECLARE_SYMBKEYHANDLER(degree, "°", 0)
        DECLARE_SYMBKEYHANDLER(pi, "π", 1)
        DECLARE_SYMBKEYHANDLER(delta, "Δ", 0)
        DECLARE_SYMBKEYHANDLER(at, "@", 0)
        DECLARE_SYMBKEYHANDLER(and, "&", 0)
        DECLARE_SYMBKEYHANDLER(econst, "е", 0)
        DECLARE_SYMBKEYHANDLER(iconst, "і", 0)
        DECLARE_SYMBKEYHANDLER(jconst, "ј", 0)
        DECLARE_SYMBKEYHANDLER(greekalpha, "α", 0)
        DECLARE_SYMBKEYHANDLER(greekbeta, "β", 0)
        DECLARE_SYMBKEYHANDLER(greekgamma, "γ", 0)
        DECLARE_SYMBKEYHANDLER(greekdelta, "δ", 0)
        DECLARE_SYMBKEYHANDLER(greekepsilon, "ε", 0)
        DECLARE_SYMBKEYHANDLER(greeketa, "η", 0)
        DECLARE_SYMBKEYHANDLER(greekrho, "ρ", 0)
        DECLARE_SYMBKEYHANDLER(greeksigma, "σ", 0)
        DECLARE_SYMBKEYHANDLER(greektau, "τ", 0)
        DECLARE_SYMBKEYHANDLER(greektheta, "θ", 0)
        DECLARE_SYMBKEYHANDLER(greeklambda, "λ", 0)
        DECLARE_SYMBKEYHANDLER(greekkappa, "κ", 0)
        DECLARE_SYMBKEYHANDLER(greekmu, "μ", 0)
        DECLARE_SYMBKEYHANDLER(greeknu, "ν", 0)
        DECLARE_SYMBKEYHANDLER(greekphi, "φ", 0)
        DECLARE_SYMBKEYHANDLER(greekomega, "ω", 0)
        DECLARE_SYMBKEYHANDLER(greekgammacap, "Γ", 0)
        DECLARE_SYMBKEYHANDLER(greeklambdacap, "Λ", 0)
        DECLARE_SYMBKEYHANDLER(greekomegacap, "Ω", 0)
        DECLARE_SYMBKEYHANDLER(greekpicap, "Π", 0)
        DECLARE_SYMBKEYHANDLER(greeksigmacap, "Σ", 0)
        DECLARE_SYMBKEYHANDLER(greekthetacap, "Θ", 0)
        DECLARE_SYMBKEYHANDLER(greekphicap, "Φ", 0)
        DECLARE_SYMBKEYHANDLER(micro, "µ", 0)
        DECLARE_VARKEYHANDLER(var1_1, 1, 0)
        DECLARE_VARKEYHANDLER(var2_1, 1, 1)
        DECLARE_VARKEYHANDLER(var3_1, 1, 2)
        DECLARE_VARKEYHANDLER(var4_1, 1, 3)
        DECLARE_VARKEYHANDLER(var5_1, 1, 4)
        DECLARE_VARKEYHANDLER(var6_1, 1, 5)
        DECLARE_VARKEYHANDLER(var1, 2, 0)
        DECLARE_VARKEYHANDLER(var2, 2, 1)
        DECLARE_VARKEYHANDLER(var3, 2, 2)
        DECLARE_VARKEYHANDLER(var4, 2, 3)
        DECLARE_VARKEYHANDLER(var5, 2, 4)
        DECLARE_VARKEYHANDLER(var6, 2, 5)
     void underscoreKeyHandler(WORD keymsg)
{
    symbolKeyHandler(keymsg, (byte_p) "_", 0);

    if((halGetCmdLineMode() != 'L') && (halGetCmdLineMode() != 'C')) {
        uiInsertCharacters((byte_p) "[]");
        uiCursorLeft(1);
        halSetCmdLineMode('A');
    }
}

void spcKeyHandler(WORD keymsg)
{
    if(halGetContext() & CONTEXT_INTSTACK) {

        // SELECTION MODE
        switch (halScreen.StkSelStatus) {
        case 0:
            // NOTHING SELECTED YET
            halScreen.StkSelStart =
                    (halScreen.StkPointer ? halScreen.StkPointer : 1);
            if(halScreen.StkSelStart > rplDepthData())
                halScreen.StkSelStart = rplDepthData();
            ++halScreen.StkSelStatus;
            halScreen.DirtyFlag |= STACK_DIRTY;
            break;
        case 1:
            // START WAS SELECTED
            if(halScreen.StkSelStart > halScreen.StkPointer) {
                halScreen.StkSelEnd = halScreen.StkSelStart;
                halScreen.StkSelStart =
                        (halScreen.StkPointer ? halScreen.StkPointer : 1);
            }
            else {
                halScreen.StkSelEnd =
                        (halScreen.StkPointer ? halScreen.StkPointer : 1);
                if(halScreen.StkSelEnd > rplDepthData())
                    halScreen.StkSelEnd = rplDepthData();

            }
            ++halScreen.StkSelStatus;
            halScreen.DirtyFlag |= STACK_DIRTY;
            break;
        case 2:
            // BOTH START AND END SELECTED, JUST CLEAR THE SELECTION
            halScreen.StkSelStatus = 0;
            halScreen.DirtyFlag |= STACK_DIRTY;
            break;
        }

        return;

    }

    symbolKeyHandler(keymsg, (byte_p) " ", 0);

}

// INTERACTIVE STACK ONLY
void tolistKeyHandler(WORD keymsg)
{
    UNUSED(keymsg);

    switch (halScreen.StkSelStatus) {
    case 0:
        // NO ITEM SELECTED, MAKE A ONE-ELEMENT LIST
        if((rplDepthData() >= halScreen.StkPointer)
                && (halScreen.StkPointer > 0)) {

            word_p newlist = rplCreateListN(1, halScreen.StkPointer, 0);
            if(!newlist || Exceptions) {
                rplBlameError(0);
                return;
            }
            rplOverwriteData(halScreen.StkPointer, newlist);

        }
        break;

    case 1:
    {
        // MAKE A LIST BETWEEN SELSTART AND STKPOINTER
        int32_t endlvl, stlvl;

        if(halScreen.StkPointer > halScreen.StkSelStart) {
            stlvl = halScreen.StkSelStart;
            endlvl = (halScreen.StkPointer >
                    rplDepthData())? rplDepthData() : halScreen.StkPointer;
        }
        else {
            endlvl = halScreen.StkSelStart;
            stlvl = (halScreen.StkPointer > 0) ? halScreen.StkPointer : 1;
        }

        // MAKE A LIST
        word_p newlist = rplCreateListN(endlvl - stlvl + 1, stlvl, 0);
        if(!newlist || Exceptions) {
            rplBlameError(0);
            return;
        }
        rplOverwriteData(stlvl, newlist);
        if(endlvl - stlvl > 0)
            rplRemoveAtData(stlvl + 1, endlvl - stlvl);
        // AND END THE SELECTION
        halScreen.StkPointer = stlvl;
        halScreen.StkVisibleLvl = -1;
        halScreen.StkSelStatus = 0;

        break;
    }

    case 2:
        // START AND END SELECTED, MOVE THE BLOCK INTO A LIST AT CURSOR
    {
        // MAKE A LIST BETWEEN SELSTART AND SELEND
        int32_t endlvl, stlvl;
        endlvl = halScreen.StkSelEnd;
        stlvl = halScreen.StkSelStart;

        // MAKE A LIST
        word_p newlist = rplCreateListN(endlvl - stlvl + 1, stlvl, 0);
        if(!newlist || Exceptions) {
            rplBlameError(0);
            return;
        }

        if(halScreen.StkPointer > endlvl) {
            int32_t lstlvl =
                    (halScreen.StkPointer >
                    rplDepthData())? rplDepthData() : halScreen.StkPointer;
            // MAKE ROOM
            memmovew(DSTop - lstlvl + 1, DSTop - lstlvl,
                    (lstlvl - endlvl) * sizeof(word_p) / sizeof(WORD));
            // INSERT THE LIST
            rplOverwriteData(lstlvl, newlist);
            // REMOVE THE ORIGINAL ITEMS
            if(endlvl > stlvl)
                rplRemoveAtData(stlvl, endlvl - stlvl);

            halScreen.StkPointer -= (endlvl - stlvl);
        }
        else if(halScreen.StkPointer < stlvl) {
            int32_t lstlvl;
            if(halScreen.StkPointer > 0) {
                lstlvl = halScreen.StkPointer;
                // MAKE ROOM, USE STACK SLACK TEMPORARILY
                memmovew(DSTop, DSTop - 1,
                        lstlvl * sizeof(word_p) / sizeof(WORD));
                // INSERT THE LIST
                rplOverwriteData(lstlvl, newlist);

                ++DSTop;
            }
            else
                rplPushData(newlist);
            // REMOVE THE ORIGINAL ITEMS
            if(endlvl >= stlvl)
                rplRemoveAtData(stlvl + 1, endlvl - stlvl + 1);

        }
        else {
            // POINTER IS WITHIN THE BLOCK
            rplOverwriteData(endlvl, newlist);
            // REMOVE THE ORIGINAL ITEMS
            if(endlvl > stlvl)
                rplRemoveAtData(stlvl, endlvl - stlvl);

            halScreen.StkPointer = stlvl;

        }

        // AND END THE SELECTION
        halScreen.StkVisibleLvl = -1;
        halScreen.StkSelStatus = 0;

        break;

    }

    }
    halScreen.DirtyFlag |= STACK_DIRTY;
    return;
}

void tomatKeyHandler(WORD keymsg)
{
    UNUSED(keymsg);

    switch (halScreen.StkSelStatus) {
    case 0:
        // NO ITEM SELECTED, MAKE A ONE-ELEMENT MATRIX
        if((rplDepthData() >= halScreen.StkPointer)
                && (halScreen.StkPointer > 0)) {

            word_p newmat = rplMatrixFlexComposeN(halScreen.StkPointer, 1);    // MAKE A SINGLE ELEMENT VECTOR
            if(!newmat || Exceptions) {
                rplBlameError(0);
                return;
            }
            rplOverwriteData(halScreen.StkPointer, newmat);

        }
        break;

    case 1:
    {
        // MAKE A LIST BETWEEN SELSTART AND STKPOINTER
        int32_t endlvl, stlvl;

        if(halScreen.StkPointer > halScreen.StkSelStart) {
            stlvl = halScreen.StkSelStart;
            endlvl = (halScreen.StkPointer >
                    rplDepthData())? rplDepthData() : halScreen.StkPointer;
        }
        else {
            endlvl = halScreen.StkSelStart;
            stlvl = (halScreen.StkPointer > 0) ? halScreen.StkPointer : 1;
        }

        // MAKE A LIST
        word_p newmat = rplMatrixFlexComposeN(stlvl, endlvl - stlvl + 1);
        if(!newmat || Exceptions) {
            rplBlameError(0);
            return;
        }
        rplOverwriteData(stlvl, newmat);
        if(endlvl - stlvl > 0)
            rplRemoveAtData(stlvl + 1, endlvl - stlvl);
        // AND END THE SELECTION
        halScreen.StkPointer = stlvl;
        halScreen.StkVisibleLvl = -1;
        halScreen.StkSelStatus = 0;

        break;
    }

    case 2:
        // START AND END SELECTED, MOVE THE BLOCK INTO A LIST AT CURSOR
    {
        // MAKE A LIST BETWEEN SELSTART AND SELEND
        int32_t endlvl, stlvl;
        endlvl = halScreen.StkSelEnd;
        stlvl = halScreen.StkSelStart;

        // MAKE A LIST
        word_p newmat = rplMatrixFlexComposeN(stlvl, endlvl - stlvl + 1);
        if(!newmat || Exceptions) {
            rplBlameError(0);
            return;
        }

        if(halScreen.StkPointer > endlvl) {
            int32_t lstlvl =
                    (halScreen.StkPointer >
                    rplDepthData())? rplDepthData() : halScreen.StkPointer;
            // MAKE ROOM
            memmovew(DSTop - lstlvl + 1, DSTop - lstlvl,
                    (lstlvl - endlvl) * sizeof(word_p) / sizeof(WORD));
            // INSERT THE LIST
            rplOverwriteData(lstlvl, newmat);
            // REMOVE THE ORIGINAL ITEMS
            if(endlvl > stlvl)
                rplRemoveAtData(stlvl, endlvl - stlvl);

            halScreen.StkPointer -= (endlvl - stlvl);
        }
        else if(halScreen.StkPointer < stlvl) {
            int32_t lstlvl;
            if(halScreen.StkPointer > 0) {
                lstlvl = halScreen.StkPointer;
                // MAKE ROOM, USE STACK SLACK TEMPORARILY
                memmovew(DSTop, DSTop - 1,
                        lstlvl * sizeof(word_p) / sizeof(WORD));
                // INSERT THE LIST
                rplOverwriteData(lstlvl, newmat);

                ++DSTop;
            }
            else
                rplPushData(newmat);
            // REMOVE THE ORIGINAL ITEMS
            if(endlvl >= stlvl)
                rplRemoveAtData(stlvl + 1, endlvl - stlvl + 1);

        }
        else {
            // POINTER IS WITHIN THE BLOCK
            rplOverwriteData(endlvl, newmat);
            // REMOVE THE ORIGINAL ITEMS
            if(endlvl > stlvl)
                rplRemoveAtData(stlvl, endlvl - stlvl);

            halScreen.StkPointer = stlvl;

        }

        // AND END THE SELECTION
        halScreen.StkVisibleLvl = -1;
        halScreen.StkSelStatus = 0;

        break;

    }

    }
    halScreen.DirtyFlag |= STACK_DIRTY;
    return;
}

// INTERACTIVE STACK ONLY
void tocplxKeyHandler(WORD keymsg)
{
    UNUSED(keymsg);

    switch (halScreen.StkSelStatus) {
    case 0:
        // NO ITEM SELECTED, DO NOTHING
        break;

    case 1:
    {
        // MAKE A LIST BETWEEN SELSTART AND STKPOINTER
        int32_t endlvl, stlvl;

        if(halScreen.StkPointer > halScreen.StkSelStart) {
            stlvl = halScreen.StkSelStart;
            endlvl = (halScreen.StkPointer >
                    rplDepthData())? rplDepthData() : halScreen.StkPointer;
        }
        else {
            endlvl = halScreen.StkSelStart;
            stlvl = (halScreen.StkPointer > 0) ? halScreen.StkPointer : 1;
        }

        if(endlvl - stlvl != 1)
            break;      // DO-NOTHING IF MORE THAN 2 ITEMS ARE SELECTED

        word_p real, imag;
        int32_t angmode;
        real = rplPeekData(endlvl);
        imag = rplPeekData(stlvl);
        if(!ISNUMBER(*real)) {
            rplError(ERR_NOTALLOWEDINCOMPLEX);
            rplBlameError(0);
            break;
        }
        angmode = ANGLEMODE(*imag);
        if(!(ISNUMBER(*imag) || ISANGLE(*imag))) {
            rplError(ERR_NOTALLOWEDINCOMPLEX);
            rplBlameError(0);
            break;
        }

        REAL re, im;

        rplReadNumberAsReal(real, &re);
        rplReadNumberAsReal(imag, &im);

        word_p newcplx = rplNewComplex(&re, &im, angmode);

        // MAKE THE COMPLEX NUMBER
        if(!newcplx || Exceptions) {
            rplBlameError(0);
            return;
        }

        rplOverwriteData(stlvl, newcplx);
        if(endlvl - stlvl > 0)
            rplRemoveAtData(stlvl + 1, endlvl - stlvl);
        // AND END THE SELECTION
        halScreen.StkPointer = stlvl;
        halScreen.StkVisibleLvl = -1;
        halScreen.StkSelStatus = 0;

        break;
    }

    case 2:
        // START AND END SELECTED, MOVE THE BLOCK INTO A LIST AT CURSOR
    {
        // MAKE A LIST BETWEEN SELSTART AND SELEND
        int32_t endlvl, stlvl;
        endlvl = halScreen.StkSelEnd;
        stlvl = halScreen.StkSelStart;

        if(endlvl - stlvl != 1)
            break;      // DO-NOTHING IF MORE THAN 2 ITEMS ARE SELECTED

        word_p real, imag;
        int32_t angmode;
        real = rplPeekData(endlvl);
        imag = rplPeekData(stlvl);
        if(!ISNUMBER(*real)) {
            rplError(ERR_NOTALLOWEDINCOMPLEX);
            rplBlameError(0);
            break;
        }
        angmode = ANGLEMODE(*imag);
        if(!(ISNUMBER(*imag) || ISANGLE(*imag))) {
            rplError(ERR_NOTALLOWEDINCOMPLEX);
            rplBlameError(0);
            break;
        }

        REAL re, im;

        rplReadNumberAsReal(real, &re);
        rplReadNumberAsReal(imag, &im);

        word_p newcplx = rplNewComplex(&re, &im, angmode);

        // MAKE THE COMPLEX NUMBER
        if(!newcplx || Exceptions) {
            rplBlameError(0);
            return;
        }

        if(halScreen.StkPointer > endlvl) {
            int32_t lstlvl =
                    (halScreen.StkPointer >
                    rplDepthData())? rplDepthData() : halScreen.StkPointer;
            // MAKE ROOM
            memmovew(DSTop - lstlvl + 1, DSTop - lstlvl,
                    (lstlvl - endlvl) * sizeof(word_p) / sizeof(WORD));
            // INSERT THE LIST
            rplOverwriteData(lstlvl, newcplx);
            // REMOVE THE ORIGINAL ITEMS
            if(endlvl > stlvl)
                rplRemoveAtData(stlvl, endlvl - stlvl);

            halScreen.StkPointer -= (endlvl - stlvl);
        }
        else if(halScreen.StkPointer < stlvl) {
            int32_t lstlvl;
            if(halScreen.StkPointer > 0) {
                lstlvl = halScreen.StkPointer;
                // MAKE ROOM, USE STACK SLACK TEMPORARILY
                memmovew(DSTop, DSTop - 1,
                        lstlvl * sizeof(word_p) / sizeof(WORD));
                // INSERT THE LIST
                rplOverwriteData(lstlvl, newcplx);

                ++DSTop;
            }
            else
                rplPushData(newcplx);
            // REMOVE THE ORIGINAL ITEMS
            if(endlvl >= stlvl)
                rplRemoveAtData(stlvl + 1, endlvl - stlvl + 1);

        }
        else {
            // POINTER IS WITHIN THE BLOCK
            rplOverwriteData(endlvl, newcplx);
            // REMOVE THE ORIGINAL ITEMS
            if(endlvl > stlvl)
                rplRemoveAtData(stlvl, endlvl - stlvl);

            halScreen.StkPointer = stlvl;

        }

        // AND END THE SELECTION
        halScreen.StkVisibleLvl = -1;
        halScreen.StkSelStatus = 0;

        break;

    }

    }
    halScreen.DirtyFlag |= STACK_DIRTY;
    return;
}

void explodeKeyHandler(WORD keymsg)
{
    UNUSED(keymsg);

    int32_t endlvl, stlvl;

    endlvl = stlvl = -1;

    switch (halScreen.StkSelStatus) {
    case 0:
        // NO ITEM SELECTED, EXPLODE ITEM AT CURSOR
        if((rplDepthData() >= halScreen.StkPointer)
                && (halScreen.StkPointer > 0)) {

            word_p obj = rplPeekData(halScreen.StkPointer);

            if(ISMATRIX(*obj) || ISLIST(*obj) || ISCOMPLEX(*obj))
                stlvl = endlvl = halScreen.StkPointer;
        }
        break;
    case 1:
        if(halScreen.StkPointer > halScreen.StkSelStart) {
            stlvl = halScreen.StkSelStart;
            endlvl = (halScreen.StkPointer >
                    rplDepthData())? rplDepthData() : halScreen.StkPointer;
        }
        else {
            endlvl = halScreen.StkSelStart;
            stlvl = (halScreen.StkPointer > 0) ? halScreen.StkPointer : 1;
        }

        break;
    case 2:
        endlvl = halScreen.StkSelEnd;
        stlvl = halScreen.StkSelStart;
        break;
    }

    if(endlvl < 0)
        return; // NOTHING TO DO

    int32_t c, totalelem = 0;

    for(c = endlvl; c >= stlvl; --c) {

        // EXPLODE ALL SELECTED ITEMS
        word_p obj = rplPeekData(c);
        int32_t nelem;

        if(ISMATRIX(*obj)) {
            nelem = rplMatrixRows(obj);
            if(!nelem)
                nelem = rplMatrixCols(obj);
        }
        else if(ISLIST(*obj)) {
            nelem = rplListLength(obj);
        }
        else if(ISCOMPLEX(*obj))
            nelem = 2;
        else {
            // NOTHING TO EXPLODE
            ++totalelem;
            continue;
        }

        totalelem += nelem;

        rplExpandStack(nelem);
        if(Exceptions) {
            rplBlameError(0);
            return;
        }

        // MAKE ROOM IN THE STACK TO START EXPLODING
        memmovew(DSTop - c + nelem, DSTop - c + 1,
                (c - 1) * sizeof(word_p) / sizeof(WORD));

        // NOW EXPLODE THE MAIN OBJECT IN-PLACE
        word_p *ptr = DSTop - c;

        obj = *ptr;     // READ AGAIN AS THERE MIGHT'VE BEEN A GC DURING EXPANDSTACK

        if(ISMATRIX(*obj)) {
            int32_t rows = rplMatrixRows(obj);
            if(!rows) {
                // EXPAND BY ELEMENTS
                int32_t k;
                for(k = 1; k <= nelem; ++k)
                    *ptr++ = rplMatrixFastGet(obj, 1, k);
            }
            else {
                // TODO: EXPAND BY ROWS - MUCH MORE DIFFICULT THAN LISTS AS IT REQUIRES CREATING NEW MATRIX OBJECTS FOR THE ROWS

                // COMPUTE SIZE OF INDIVIDUAL ROWS
                int32_t cols = rplMatrixCols(obj);
                int32_t totalsize = (2 + cols) * rows;     // ACCOUNT FOR PROLOG+SIZE+OFFSET TABLE OF ALL ROWS

                int32_t i, j, k;

                for(i = 1; i <= rows; ++i) {
                    for(j = 1; j <= cols; ++j) {
                        for(k = 1; k < j; ++k)
                            if(rplMatrixFastGet(obj, i,
                                        j) == rplMatrixFastGet(obj, i, k))
                                break;
                        if(k == j)
                            totalsize += rplObjSize(rplMatrixFastGet(obj, i, j));       // ONLY COUNT NON-REPEATED OBJECTS
                    }
                }

                // HERE WE HAVE TOTAL SIZE OF ALL ROWS TO BE EXPANDED
                // ALLOCATE ONE BIG BLOCK OF MEMORY FOR ALL ROWS

                word_p newrows = rplAllocTempOb(totalsize - 1);
                if(!newrows) {
                    // CLOSE THE STACK BACK BEFORE RETURNING
                    memmovew(DSTop - c + 1, DSTop - c + nelem,
                            (c - 1) * sizeof(word_p) / sizeof(WORD));
                    rplBlameError(0);
                    return;
                }

                obj = *ptr;     // READ AGAIN AS THERE MIGHT'VE BEEN A GC DURING ALLOCATION

                word_p rptr = newrows, objptr;

                for(i = 1; i <= rows; ++i) {
                    rptr[1] = MATMKSIZE(0, cols);
                    objptr = rptr + 2 + cols;   // POINT TO THE LOCATION OF THE NEXT OBJECT TO BE STORED
                    for(j = 1; j <= cols; ++j) {
                        for(k = 1; k < j; ++k)
                            if(rplMatrixFastGet(obj, i,
                                        j) == rplMatrixFastGet(obj, i, k))
                                break;
                        if(k == j) {
                            rplCopyObject(objptr, rplMatrixFastGet(obj, i, j)); // ADD A NEW OBJECT
                            rptr[1 + j] = objptr - rptr;
                            objptr = rplSkipOb(objptr);
                        }
                        else
                            rptr[1 + j] = rptr[1 + k];  // REUSE THE OBJECT
                    }
                    // DONE WITH THE ROW, FIX THE SIZE
                    rptr[0] = MKPROLOG(DOMATRIX, objptr - rptr - 1);
                    rptr = objptr;
                }

                // NOW EXPAND IT ON THE STACK LIKE A LIST

                rptr = newrows;
                for(i = 0; i < rows; ++i) {
                    *ptr++ = rptr;
                    rptr = rplSkipOb(rptr);
                }

            }

        }
        else if(ISLIST(*obj)) {
            int32_t k;
            word_p item = obj + 1;
            for(k = 0; k < nelem; ++k) {
                *ptr++ = item;
                item = rplSkipOb(item);
            }
        }
        else if(ISCOMPLEX(*obj)) {
            *ptr++ = obj + 1;
            *ptr++ = rplSkipOb(obj + 1);
        }

        DSTop += nelem - 1;
        if(halScreen.StkPointer > c)
            halScreen.StkPointer += nelem - 1;
        endlvl += nelem - 1;
    }

    // DONE EXPLODING, ADJUST POINTERS TO SELECT EVERYTHING
    halScreen.StkSelStart = stlvl;
    halScreen.StkSelEnd = endlvl;

    // SPECIAL CASE: WHEN BLOCK IS SELECTED AND POINTER IS OUTSIDE THE BLOCK, MOVE EXPLODED ITEMS TO THE NEW LOCATION
    if(halScreen.StkSelStatus == 2) {
        if(halScreen.StkPointer > halScreen.StkSelEnd) {
            word_p *stptr, *endptr, *cptr;
            word_p item;
            stptr = DSTop - halScreen.StkSelStart;
            endptr = DSTop - ((halScreen.StkPointer >
                        rplDepthData())? rplDepthData() : halScreen.StkPointer);

            // DO UNROT UNTIL THE ENTIRE BLOCK MOVED
            int32_t count = halScreen.StkSelEnd - halScreen.StkSelStart + 1;

            while(count--) {
                cptr = stptr;

                item = *cptr;

                while(cptr > endptr) {
                    cptr[0] = cptr[-1];
                    --cptr;
                }
                *cptr = item;
            }

            count = halScreen.StkSelEnd - halScreen.StkSelStart;
            halScreen.StkSelEnd =
                    ((halScreen.StkPointer >
                        rplDepthData())? rplDepthData() : halScreen.StkPointer);
            halScreen.StkSelStart = halScreen.StkSelEnd - count;

        }
        else if(halScreen.StkPointer < halScreen.StkSelStart) {

            word_p *stptr, *endptr, *cptr;
            word_p item;
            stptr = DSTop - halScreen.StkSelEnd;
            endptr = DSTop - halScreen.StkPointer - 1;

            // DO ROT UNTIL THE ENTIRE BLOCK MOVED
            int32_t count = halScreen.StkSelEnd - halScreen.StkSelStart + 1;
            while(count--) {
                cptr = stptr;

                item = *cptr;

                while(cptr < endptr) {
                    cptr[0] = cptr[1];
                    ++cptr;
                }
                *cptr = item;
            }

            count = halScreen.StkSelEnd - halScreen.StkSelStart;
            halScreen.StkSelStart = halScreen.StkPointer + 1;
            halScreen.StkSelEnd = halScreen.StkPointer + 1 + count;
            halScreen.StkPointer += count + 1;
            halScreen.StkVisibleLvl = -1;

        }
    }

    if(stlvl == endlvl)
        halScreen.StkSelStatus = 0;
    else
        halScreen.StkSelStatus = 2;
    halScreen.StkPointer = halScreen.StkSelEnd;
    halScreen.StkVisibleLvl = -1;

    halScreen.DirtyFlag |= STACK_DIRTY;
    return;
}

DECLARE_SYMBKEYHANDLER(thinspc, " ", 0)
DECLARE_SYMBKEYHANDLER(hash, "#", 0)
DECLARE_SYMBKEYHANDLER(equal, "=", 1)
DECLARE_SYMBKEYHANDLER(notequal, "≠", 1)
DECLARE_SYMBKEYHANDLER(ls, "<", 1)
DECLARE_SYMBKEYHANDLER(gt, ">", 1)
DECLARE_SYMBKEYHANDLER(le, "≤", 1)
DECLARE_SYMBKEYHANDLER(ge, "≥", 1)
DECLARE_SYMBKEYHANDLER(sadd, "+", 0)
DECLARE_SYMBKEYHANDLER(ssub, "-", 0)
DECLARE_SYMBKEYHANDLER(smul, "*", 0)
DECLARE_SYMBKEYHANDLER(sdiv, "/", 0)
DECLARE_SYMBKEYHANDLER(spow, "^", 0)
DECLARE_KEYHANDLER(sub0, "₀", "⁰")
DECLARE_KEYHANDLER(sub1, "₁", "¹")
DECLARE_KEYHANDLER(sub2, "₂", "²")
DECLARE_KEYHANDLER(sub3, "₃", "³")
DECLARE_KEYHANDLER(sub4, "₄", "⁴")
DECLARE_KEYHANDLER(sub5, "₅", "⁵")
DECLARE_KEYHANDLER(sub6, "₆", "⁶")
DECLARE_KEYHANDLER(sub7, "₇", "⁷")
DECLARE_KEYHANDLER(sub8, "₈", "⁸")
DECLARE_KEYHANDLER(sub9, "₉", "⁹")
DECLARE_SYMBKEYHANDLER(keyx, "X", 0)
DECLARE_CMDKEYHANDLER(clear, CMD_CLEAR, "CLEAR", -1)
DECLARE_CMDKEYHANDLER(add, (CMD_OVR_ADD), "+", 0)
DECLARE_CMDKEYHANDLER(sub, (CMD_OVR_SUB), "-", 0)
DECLARE_CMDKEYHANDLER(div, (CMD_OVR_DIV), "/", 0)
DECLARE_CMDKEYHANDLER(mul, (CMD_OVR_MUL), "*", 0)
DECLARE_CMDKEYHANDLER(fact, (CMD_FACTORIAL), "!", 0)
DECLARE_CMDKEYHANDLER(inv, (CMD_OVR_INV), "INV", 1)
DECLARE_CMDKEYHANDLER(sin, CMD_SIN, "SIN", 1)
DECLARE_CMDKEYHANDLER(asin, CMD_ASIN, "ASIN", 1)
DECLARE_CMDKEYHANDLER(sinh, CMD_SINH, "SINH", 1)
DECLARE_CMDKEYHANDLER(asinh, CMD_ASINH, "ASINH", 1)
DECLARE_CMDKEYHANDLER(cos, CMD_COS, "COS", 1)
DECLARE_CMDKEYHANDLER(acos, CMD_ACOS, "ACOS", 1)
DECLARE_CMDKEYHANDLER(cosh, CMD_COSH, "COSH", 1)
DECLARE_CMDKEYHANDLER(acosh, CMD_ACOSH, "ACOSH", 1)
DECLARE_CMDKEYHANDLER(tan, CMD_TAN, "TAN", 1)
DECLARE_CMDKEYHANDLER(atan, CMD_ATAN, "ATAN", 1)
DECLARE_CMDKEYHANDLER(tanh, CMD_TANH, "TANH", 1)
DECLARE_CMDKEYHANDLER(atanh, CMD_ATANH, "ATANH", 1)
DECLARE_CMDKEYHANDLER(eval, (CMD_OVR_EVAL), "EVAL", -1)
DECLARE_CMDKEYHANDLER(eval1, (CMD_OVR_EVAL1), "EVAL1", -1)
DECLARE_CMDKEYHANDLER(tonum, (CMD_OVR_NUM), "→NUM", -1)
DECLARE_CMDKEYHANDLER(tofrac, (CMD_TOFRACTION), "→Q", -1)
DECLARE_CMDKEYHANDLER(sqrt, CMD_SQRT, "√", 0)
DECLARE_CMDKEYHANDLER(pow, (CMD_OVR_POW), "^", 0)
DECLARE_CMDKEYHANDLER(ln, CMD_LN, "LN", 1)
DECLARE_CMDKEYHANDLER(exp, CMD_EXP, "EXP", 1)
DECLARE_CMDKEYHANDLER(log, CMD_LOG, "LOG", 1)
DECLARE_CMDKEYHANDLER(alog, CMD_ALOG, "ALOG", 1)
DECLARE_CMDKEYHANDLER(sq, CMD_SQ, "SQ", 1)
DECLARE_CMDKEYHANDLER(xroot, (CMD_OVR_XROOT), "XROOT", 1)
DECLARE_CMDKEYHANDLER(sto, CMD_STO, "STO", 2)
DECLARE_CMDKEYHANDLER(rcl, CMD_RCL, "RCL", 2)
DECLARE_CMDKEYHANDLER(purge, CMD_PURGE, "PURGE", -1)
DECLARE_CMDKEYHANDLER(abs, CMD_OVR_ABS, "ABS", 1)
DECLARE_CMDKEYHANDLER(arg, CMD_ARG, "ARG", 1)
DECLARE_CMDKEYHANDLER(convert, CMD_CONVERT, "CONVERT", -1)
DECLARE_CMDKEYHANDLER(cont, CMD_CONT, "CONT", -1)
DECLARE_TRANSPCMDKEYHANDLER(updir, CMD_UPDIR)
DECLARE_TRANSPCMDKEYHANDLER(home, CMD_HOME)
DECLARE_TRANSPCMDKEYHANDLER(menuswap, CMD_MENUSWAP)
DECLARE_MENUKEYHANDLER(unitmenu, MKMENUCODE(0, DOUNIT, 0, 0))
DECLARE_MENUKEYHANDLER(prgmenu, MKMENUCODE(0, 68, 3, 0))
DECLARE_MENUKEYHANDLER(varsmenu, MKMENUCODE(1, 0, 0, 0))
DECLARE_MENUKEYHANDLER(mainmenu, MKMENUCODE(0, 68, 2, 0))
DECLARE_MENUKEYHANDLER(arithmenu, MKMENUCODE(0, 64, 0, 0))
DECLARE_MENUKEYHANDLER(cplxmenu, MKMENUCODE(0, 30, 0, 0))
DECLARE_MENUKEYHANDLER(timemenu, MKMENUCODE(0, 65, 0, 0))
DECLARE_MENUKEYHANDLER(basemenu, MKMENUCODE(0, 70, 0, 0))
DECLARE_MENUKEYHANDLER(libsmenu, MKMENUCODE(2, 0, 0, 0))
DECLARE_MENUKEYHANDLER(numsolvermenu, MKMENUCODE(0, 104, 0, 0))
DECLARE_MENUKEYHANDLER(financemenu, MKMENUCODE(0, 104, 1, 0))

#ifdef TARGET_PRIME1
void powerOffKeyHandler(WORD keymsg)
{
    UNUSED(keymsg);
    // SHIFT-ON MEANS POWER OFF!
    halPreparePowerOff();
    halEnterPowerOff();
    return;
}
#endif /* TARGET_PRIME1 */

void cancelKeyHandler(WORD keymsg)
{
    UNUSED(keymsg);

#ifndef TARGET_PRIME1
    if(halGetNotification(N_RIGHTSHIFT)) {
        // SHIFT-ON MEANS POWER OFF!
        halPreparePowerOff();
        halEnterPowerOff();
        return;
    }
#endif /* TARGET_PRIME1 */

    if(halGetNotification(N_LEFTSHIFT)) {
        // THIS IS CONTINUE
        contKeyHandler(keymsg);
        keyb_setshiftplane(0, 0, 0, 0);
        return;
    }

    if((halGetContext() & CONTEXT_INEDITOR)) {
        // END THE COMMAND LINE
        endCmdLine();
    }

    if((halGetContext() & CONTEXT_INTSTACK)) {
        // END INTERACTIVE STACK
        halSetContext((halGetContext() & ~CONTEXT_INTSTACK) | CONTEXT_STACK);
        halScreen.StkVisibleLvl = 1;
        halScreen.StkVisibleOffset = 0;
        halScreen.StkSelStart = halScreen.StkSelEnd = halScreen.StkSelStatus =
                0;
        halScreen.DirtyFlag |= STACK_DIRTY;
    }

}


// **************************************************************************
// ******************* END OF DEFAULT KEY HANDLERS **************************
// **************************************************************************

typedef void (*handlerfunc_t)(WORD keymsg);

// STRUCTURE FOR DEFAULT KEYBOARD HANDLERS
struct keyhandler_t
{
    WORD message;
    int32_t context;
    handlerfunc_t action;
};

// LIST OF HANDLERS, END WITH action=NULL
const struct keyhandler_t const keydefaulthandlers[] = {

    // BASIC NUMBERS
    {KM_PRESS | KB_1, CONTEXT_ANY, &numberKeyHandler},
    {KM_PRESS | KB_2, CONTEXT_ANY, &numberKeyHandler},
    {KM_PRESS | KB_3, CONTEXT_ANY, &numberKeyHandler},
    {KM_PRESS | KB_4, CONTEXT_ANY, &numberKeyHandler},
    {KM_PRESS | KB_5, CONTEXT_ANY, &numberKeyHandler},
    {KM_PRESS | KB_6, CONTEXT_ANY, &numberKeyHandler},
    {KM_PRESS | KB_7, CONTEXT_ANY, &numberKeyHandler},
    {KM_PRESS | KB_8, CONTEXT_ANY, &numberKeyHandler},
    {KM_PRESS | KB_9, CONTEXT_ANY, &numberKeyHandler},
    {KM_PRESS | KB_0, CONTEXT_ANY, &numberKeyHandler},
    {KM_PRESS | KB_DOT, CONTEXT_ANY, &decimaldotKeyHandler},
    {KM_PRESS | KB_DOT | SHIFT_ALPHAHOLD, CONTEXT_ANY, &dotKeyHandler},
    {KM_PRESS | KB_1 | SHIFT_ALPHA, CONTEXT_ANY, &numberKeyHandler},
    {KM_PRESS | KB_2 | SHIFT_ALPHA, CONTEXT_ANY, &numberKeyHandler},
    {KM_PRESS | KB_3 | SHIFT_ALPHA, CONTEXT_ANY, &numberKeyHandler},
    {KM_PRESS | KB_4 | SHIFT_ALPHA, CONTEXT_ANY, &numberKeyHandler},
    {KM_PRESS | KB_5 | SHIFT_ALPHA, CONTEXT_ANY, &numberKeyHandler},
    {KM_PRESS | KB_6 | SHIFT_ALPHA, CONTEXT_ANY, &numberKeyHandler},
    {KM_PRESS | KB_7 | SHIFT_ALPHA, CONTEXT_ANY, &numberKeyHandler},
    {KM_PRESS | KB_8 | SHIFT_ALPHA, CONTEXT_ANY, &numberKeyHandler},
    {KM_PRESS | KB_9 | SHIFT_ALPHA, CONTEXT_ANY, &numberKeyHandler},
    {KM_PRESS | KB_0 | SHIFT_ALPHA, CONTEXT_ANY, &numberKeyHandler},
    {KM_PRESS | KB_DOT | SHIFT_ALPHA, CONTEXT_ANY, &decimaldotKeyHandler},

// BASIC ON AND SHIFTS
#ifndef TARGET_PRIME1
    {KM_KEYDN | KB_ON, CONTEXT_ANY, &cancelKeyHandler},
#else /* TARGET_PRIME1 */
    {KM_PRESS | KB_ON, CONTEXT_ANY, &cancelKeyHandler},
    {KM_PRESS | KB_ON | SHIFT_ALPHA, CONTEXT_ANY, &cancelKeyHandler},
    {KM_PRESS | KB_ON | SHIFT_ALPHA | SHIFT_LS, CONTEXT_ANY, &cancelKeyHandler},
    {KM_PRESS | KB_ON | SHIFT_ALPHA | SHIFT_RS, CONTEXT_ANY, &cancelKeyHandler},
    {KM_PRESS | KB_ON | SHIFT_LS, CONTEXT_ANY, &cancelKeyHandler},
    {KM_PRESS | KB_ON | SHIFT_RS, CONTEXT_ANY, &cancelKeyHandler},

    {KM_PRESS | KB_RSHIFT | SHIFT_ONHOLD, CONTEXT_ANY, &powerOffKeyHandler},
    {KM_PRESS | KB_RSHIFT | SHIFT_ONHOLD | SHIFT_ALPHA, CONTEXT_ANY, &powerOffKeyHandler},
#endif /* TARGET_PRIME1 */

    {KM_PRESS | KB_ALPHA | SHIFT_RS, CONTEXT_ANY, &shiftedalphaKeyHandler},
    {KM_PRESS | KB_ALPHA | SHIFT_RSHOLD, CONTEXT_ANY, &shiftedalphaKeyHandler},

// TEXT EDITING KEYS
    {KM_PRESS | KB_ENT, CONTEXT_ANY, &enterKeyHandler},
    {KM_PRESS | KB_ENT | SHIFT_ALPHA, CONTEXT_ANY, &enterKeyHandler},
    {KM_PRESS | KB_ENT | SHIFT_ALPHAHOLD, CONTEXT_ANY, &enterKeyHandler},
    {KM_PRESS | KB_BKS, CONTEXT_ANY, &backspKeyHandler},
    {KM_REPEAT | KB_BKS, CONTEXT_ANY, &backspKeyHandler},
    {KM_PRESS | KB_BKS | SHIFT_ALPHA, CONTEXT_ANY, &backspKeyHandler},
    {KM_REPEAT | KB_BKS | SHIFT_ALPHA, CONTEXT_ANY, &backspKeyHandler},
    {KM_PRESS | KB_BKS | SHIFT_RS, CONTEXT_ANY, &clearKeyHandler},
    {KM_PRESS | KB_BKS | SHIFT_RSHOLD, CONTEXT_ANY, &clearKeyHandler},
    {KM_PRESS | KB_BKS | SHIFT_LS, CONTEXT_ANY, &deleteKeyHandler},
    {KM_PRESS | KB_BKS | SHIFT_LSHOLD, CONTEXT_ANY, &deleteKeyHandler},
    {KM_PRESS | KB_BKS | SHIFT_LS | SHIFT_ALPHA, CONTEXT_ANY,
                &deleteKeyHandler},
    {KM_PRESS | KB_BKS | SHIFT_LSHOLD | SHIFT_ALPHA, CONTEXT_ANY,
                &deleteKeyHandler},
    {KM_PRESS | KB_LF | SHIFT_LS, CONTEXT_ANY, &lsleftKeyHandler},
    {KM_PRESS | KB_RT | SHIFT_LS, CONTEXT_ANY, &lsrightKeyHandler},
    {KM_PRESS | KB_LF | SHIFT_LS | SHIFT_LSHOLD, CONTEXT_ANY,
                &copyclipKeyHandler},
    {KM_PRESS | KB_RT | SHIFT_LS | SHIFT_LSHOLD, CONTEXT_ANY,
                &pasteclipKeyHandler},
    {KM_PRESS | KB_DN | SHIFT_LS | SHIFT_LSHOLD, CONTEXT_ANY,
                &cutclipKeyHandler},
    {KM_PRESS | KB_LF | SHIFT_LS | SHIFT_ALPHA, CONTEXT_ANY, &lsleftKeyHandler},
    {KM_PRESS | KB_RT | SHIFT_LS | SHIFT_ALPHA, CONTEXT_ANY,
                &lsrightKeyHandler},
    {KM_PRESS | KB_LF | SHIFT_LS | SHIFT_LSHOLD | SHIFT_ALPHA, CONTEXT_ANY,
                &copyclipKeyHandler},
    {KM_PRESS | KB_RT | SHIFT_LS | SHIFT_LSHOLD | SHIFT_ALPHA, CONTEXT_ANY,
                &pasteclipKeyHandler},

// INTERACTIVE STACK OVERRIDES
    {KM_PRESS | KB_ADD, CONTEXT_ANY | CONTEXT_INTSTACK, &tolistKeyHandler},
    {KM_PRESS | KB_MUL, CONTEXT_ANY | CONTEXT_INTSTACK, &tomatKeyHandler},
    {KM_PRESS | KB_SUB, CONTEXT_ANY | CONTEXT_INTSTACK, &tocplxKeyHandler},
    {KM_PRESS | KB_DIV, CONTEXT_ANY | CONTEXT_INTSTACK, &explodeKeyHandler},

// CURSOR MOVEMENT KEYS
    {KM_PRESS | KB_LF, CONTEXT_ANY, &leftKeyHandler},
    {KM_REPEAT | KB_LF, CONTEXT_ANY, &leftKeyHandler},
    {KM_PRESS | KB_RT, CONTEXT_ANY, &rightKeyHandler},
    {KM_REPEAT | KB_RT, CONTEXT_ANY, &rightKeyHandler},
    {KM_PRESS | KB_LF | SHIFT_ALPHA, CONTEXT_ANY, &leftKeyHandler},
    {KM_REPEAT | KB_LF | SHIFT_ALPHA, CONTEXT_ANY, &leftKeyHandler},
    {KM_PRESS | KB_RT | SHIFT_ALPHA, CONTEXT_ANY, &rightKeyHandler},
    {KM_REPEAT | KB_RT | SHIFT_ALPHA, CONTEXT_ANY, &rightKeyHandler},
    {KM_PRESS | KB_DN, CONTEXT_ANY, &downKeyHandler},
    {KM_REPEAT | KB_DN, CONTEXT_ANY, &downKeyHandler},
    {KM_PRESS | KB_DN | SHIFT_ALPHA, CONTEXT_ANY, &downKeyHandler},
    {KM_REPEAT | KB_DN | SHIFT_ALPHA, CONTEXT_ANY, &downKeyHandler},
    {KM_PRESS | KB_UP, CONTEXT_ANY, &upKeyHandler},
    {KM_REPEAT | KB_UP, CONTEXT_ANY, &upKeyHandler},
    {KM_PRESS | KB_UP | SHIFT_ALPHA, CONTEXT_ANY, &upKeyHandler},
    {KM_REPEAT | KB_UP | SHIFT_ALPHA, CONTEXT_ANY, &upKeyHandler},

    {KM_PRESS | KB_LF | SHIFT_RS, CONTEXT_ANY, &rsleftKeyHandler},
    {KM_PRESS | KB_LF | SHIFT_RSHOLD, CONTEXT_ANY, &rsholdleftKeyHandler},
    {KM_PRESS | KB_LF | SHIFT_RS | SHIFT_ALPHA, CONTEXT_ANY, &rsleftKeyHandler},
    {KM_PRESS | KB_LF | SHIFT_RSHOLD | SHIFT_ALPHA, CONTEXT_ANY,
                &rsholdleftKeyHandler},
    {KM_PRESS | KB_RT | SHIFT_RS, CONTEXT_ANY, &rsrightKeyHandler},
    {KM_PRESS | KB_RT | SHIFT_RSHOLD, CONTEXT_ANY, &rsholdrightKeyHandler},
    {KM_PRESS | KB_RT | SHIFT_RS | SHIFT_ALPHA, CONTEXT_ANY,
                &rsrightKeyHandler},
    {KM_PRESS | KB_RT | SHIFT_RSHOLD | SHIFT_ALPHA, CONTEXT_ANY,
                &rsholdrightKeyHandler},
    {KM_PRESS | KB_RT | SHIFT_ALPHAHOLD, CONTEXT_ANY,
                &alphaholdrightKeyHandler},

    {KM_PRESS | KB_UP | SHIFT_RS, CONTEXT_ANY, &rsupKeyHandler},
    {KM_PRESS | KB_UP | SHIFT_RSHOLD, CONTEXT_ANY, &rsholdupKeyHandler},
    {KM_PRESS | KB_UP | SHIFT_RS | SHIFT_ALPHA, CONTEXT_ANY, &rsupKeyHandler},
    {KM_PRESS | KB_UP | SHIFT_RSHOLD | SHIFT_ALPHA, CONTEXT_ANY,
                &rsholdupKeyHandler},
    {KM_PRESS | KB_UP | SHIFT_ALPHAHOLD | SHIFT_ALPHA, CONTEXT_ANY,
                &alphaholdupKeyHandler},

    {KM_PRESS | KB_DN | SHIFT_RS, CONTEXT_ANY, &rsdownKeyHandler},
    {KM_PRESS | KB_DN | SHIFT_RSHOLD, CONTEXT_ANY, &rsholddownKeyHandler},
    {KM_PRESS | KB_DN | SHIFT_RS | SHIFT_ALPHA, CONTEXT_ANY, &rsdownKeyHandler},
    {KM_PRESS | KB_DN | SHIFT_RSHOLD | SHIFT_ALPHA, CONTEXT_ANY,
                &rsholddownKeyHandler},
    {KM_PRESS | KB_DN | SHIFT_ALPHAHOLD | SHIFT_ALPHA, CONTEXT_ANY,
                &alphaholddownKeyHandler},

#ifndef TARGET_PRIME1
    {KM_PRESS | KB_DOT | SHIFT_RS, CONTEXT_ANY, &newlineKeyHandler},
    {KM_PRESS | KB_DOT | SHIFT_RSHOLD, CONTEXT_ANY, &newlineKeyHandler},
    {KM_PRESS | KB_DOT | SHIFT_RS | SHIFT_ALPHA, CONTEXT_ANY, newlineKeyHandler},
    {KM_PRESS | KB_DOT | SHIFT_RSHOLD | SHIFT_ALPHA, CONTEXT_ANY, newlineKeyHandler},
#else /* TARGET_PRIME1 */
    {KM_PRESS | KB_ENT | SHIFT_RS, CONTEXT_ANY, &newlineKeyHandler},
    {KM_PRESS | KB_ENT | SHIFT_RSHOLD, CONTEXT_ANY, &newlineKeyHandler},
    {KM_PRESS | KB_ENT | SHIFT_RS | SHIFT_ALPHA, CONTEXT_ANY, newlineKeyHandler},
    {KM_PRESS | KB_ENT | SHIFT_RSHOLD | SHIFT_ALPHA, CONTEXT_ANY, newlineKeyHandler},
#endif /* TARGET_PRIME1 */

// BASIC OPERATORS
    {KM_PRESS | KB_ADD, CONTEXT_ANY, &addKeyHandler},
    {KM_PRESS | KB_SUB, CONTEXT_ANY, &subKeyHandler},
    {KM_PRESS | KB_DIV, CONTEXT_ANY, &divKeyHandler},
    {KM_PRESS | KB_MUL, CONTEXT_ANY, &mulKeyHandler},
    {KM_PRESS | KB_ADD | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(sadd)},
    {KM_PRESS | KB_SUB | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(ssub)},
#ifndef TARGET_PRIME1
    {KM_PRESS | KB_DIV | SHIFT_ALPHA | SHIFT_RS, CONTEXT_ANY, KEYHANDLER_NAME(sdiv)},
    {KM_PRESS | KB_DIV | SHIFT_ALPHA | SHIFT_RSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(sdiv)},
#else /* TARGET_PRIME1 */
    {KM_PRESS | KB_DIV | SHIFT_ALPHA | SHIFT_RS, CONTEXT_ANY, KEYHANDLER_NAME(sdiv)},
    {KM_PRESS | KB_DIV | SHIFT_ALPHA | SHIFT_RSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(sdiv)},
#endif /* TARGET_PRIME1 */
    {KM_PRESS | KB_MUL | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(smul)},

#ifndef TARGET_PRIME1
// VARS MENU KEYS
    {KM_PRESS | KB_G, CONTEXT_ANY, KEYHANDLER_NAME(var1)},
    {KM_PRESS | KB_G | SHIFT_LS, CONTEXT_ANY, KEYHANDLER_NAME(var1)},
    {KM_PRESS | KB_G | SHIFT_LSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(var1)},
    {KM_PRESS | KB_G | SHIFT_RS, CONTEXT_ANY, KEYHANDLER_NAME(var1)},
    {KM_PRESS | KB_G | SHIFT_RSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(var1)},
    {KM_LPRESS | KB_G, CONTEXT_ANY, KEYHANDLER_NAME(var1)},
    {KM_KEYUP | KB_G, CONTEXT_ANY, KEYHANDLER_NAME(var1)},

    {KM_PRESS | KB_H, CONTEXT_ANY, KEYHANDLER_NAME(var2)},
    {KM_PRESS | KB_H | SHIFT_LS, CONTEXT_ANY, KEYHANDLER_NAME(var2)},
    {KM_PRESS | KB_H | SHIFT_LSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(var2)},
    {KM_PRESS | KB_H | SHIFT_RS, CONTEXT_ANY, KEYHANDLER_NAME(var2)},
    {KM_PRESS | KB_H | SHIFT_RSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(var2)},
    {KM_LPRESS | KB_H, CONTEXT_ANY, KEYHANDLER_NAME(var2)},
    {KM_KEYUP | KB_H, CONTEXT_ANY, KEYHANDLER_NAME(var2)},

    {KM_PRESS | KB_I, CONTEXT_ANY, KEYHANDLER_NAME(var3)},
    {KM_PRESS | KB_I | SHIFT_LS, CONTEXT_ANY, KEYHANDLER_NAME(var3)},
    {KM_PRESS | KB_I | SHIFT_LSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(var3)},
    {KM_PRESS | KB_I | SHIFT_RS, CONTEXT_ANY, KEYHANDLER_NAME(var3)},
    {KM_PRESS | KB_I | SHIFT_RSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(var3)},
    {KM_LPRESS | KB_I, CONTEXT_ANY, KEYHANDLER_NAME(var3)},
    {KM_KEYUP | KB_I, CONTEXT_ANY, KEYHANDLER_NAME(var3)},

    {KM_PRESS | KB_J, CONTEXT_ANY, KEYHANDLER_NAME(var4)},
    {KM_PRESS | KB_J | SHIFT_LS, CONTEXT_ANY, KEYHANDLER_NAME(var4)},
    {KM_PRESS | KB_J | SHIFT_LSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(var4)},
    {KM_PRESS | KB_J | SHIFT_RS, CONTEXT_ANY, KEYHANDLER_NAME(var4)},
    {KM_PRESS | KB_J | SHIFT_RSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(var4)},
    {KM_LPRESS | KB_J, CONTEXT_ANY, KEYHANDLER_NAME(var4)},
    {KM_KEYUP | KB_J, CONTEXT_ANY, KEYHANDLER_NAME(var4)},

    {KM_PRESS | KB_K, CONTEXT_ANY, KEYHANDLER_NAME(var5)},
    {KM_PRESS | KB_K | SHIFT_LS, CONTEXT_ANY, KEYHANDLER_NAME(var5)},
    {KM_PRESS | KB_K | SHIFT_LSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(var5)},
    {KM_PRESS | KB_K | SHIFT_RS, CONTEXT_ANY, KEYHANDLER_NAME(var5)},
    {KM_PRESS | KB_K | SHIFT_RSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(var5)},
    {KM_LPRESS | KB_K, CONTEXT_ANY, KEYHANDLER_NAME(var5)},
    {KM_KEYUP | KB_K, CONTEXT_ANY, KEYHANDLER_NAME(var5)},

    {KM_PRESS | KB_L, CONTEXT_ANY, KEYHANDLER_NAME(var6)},
    {KM_PRESS | KB_L | SHIFT_LS, CONTEXT_ANY, KEYHANDLER_NAME(var6)},
    {KM_PRESS | KB_L | SHIFT_LSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(var6)},
    {KM_PRESS | KB_L | SHIFT_RS, CONTEXT_ANY, KEYHANDLER_NAME(var6)},
    {KM_PRESS | KB_L | SHIFT_RSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(var6)},
    {KM_LPRESS | KB_L, CONTEXT_ANY, KEYHANDLER_NAME(var6)},
    {KM_KEYUP | KB_L, CONTEXT_ANY, KEYHANDLER_NAME(var6)},

    {KM_PRESS | KB_A, CONTEXT_ANY, KEYHANDLER_NAME(var1_1)},
    {KM_PRESS | KB_A | SHIFT_LS, CONTEXT_ANY, KEYHANDLER_NAME(var1_1)},
    {KM_PRESS | KB_A | SHIFT_LSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(var1_1)},
    {KM_PRESS | KB_A | SHIFT_RS, CONTEXT_ANY, KEYHANDLER_NAME(var1_1)},
    {KM_PRESS | KB_A | SHIFT_RSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(var1_1)},
    {KM_LPRESS | KB_A, CONTEXT_ANY, KEYHANDLER_NAME(var1_1)},
    {KM_KEYUP | KB_A, CONTEXT_ANY, KEYHANDLER_NAME(var1_1)},

    {KM_PRESS | KB_B, CONTEXT_ANY, KEYHANDLER_NAME(var2_1)},
    {KM_PRESS | KB_B | SHIFT_LS, CONTEXT_ANY, KEYHANDLER_NAME(var2_1)},
    {KM_PRESS | KB_B | SHIFT_LSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(var2_1)},
    {KM_PRESS | KB_B | SHIFT_RS, CONTEXT_ANY, KEYHANDLER_NAME(var2_1)},
    {KM_PRESS | KB_B | SHIFT_RSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(var2_1)},
    {KM_LPRESS | KB_B, CONTEXT_ANY, KEYHANDLER_NAME(var2_1)},
    {KM_KEYUP | KB_B, CONTEXT_ANY, KEYHANDLER_NAME(var2_1)},

    {KM_PRESS | KB_C, CONTEXT_ANY, KEYHANDLER_NAME(var3_1)},
    {KM_PRESS | KB_C | SHIFT_LS, CONTEXT_ANY, KEYHANDLER_NAME(var3_1)},
    {KM_PRESS | KB_C | SHIFT_LSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(var3_1)},
    {KM_PRESS | KB_C | SHIFT_RS, CONTEXT_ANY, KEYHANDLER_NAME(var3_1)},
    {KM_PRESS | KB_C | SHIFT_RSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(var3_1)},
    {KM_LPRESS | KB_C, CONTEXT_ANY, KEYHANDLER_NAME(var3_1)},
    {KM_KEYUP | KB_C, CONTEXT_ANY, KEYHANDLER_NAME(var3_1)},

    {KM_PRESS | KB_D, CONTEXT_ANY, KEYHANDLER_NAME(var4_1)},
    {KM_PRESS | KB_D | SHIFT_LS, CONTEXT_ANY, KEYHANDLER_NAME(var4_1)},
    {KM_PRESS | KB_D | SHIFT_LSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(var4_1)},
    {KM_PRESS | KB_D | SHIFT_RS, CONTEXT_ANY, KEYHANDLER_NAME(var4_1)},
    {KM_PRESS | KB_D | SHIFT_RSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(var4_1)},
    {KM_LPRESS | KB_D, CONTEXT_ANY, KEYHANDLER_NAME(var4_1)},
    {KM_KEYUP | KB_D, CONTEXT_ANY, KEYHANDLER_NAME(var4_1)},

    {KM_PRESS | KB_E, CONTEXT_ANY, KEYHANDLER_NAME(var5_1)},
    {KM_PRESS | KB_E | SHIFT_LS, CONTEXT_ANY, KEYHANDLER_NAME(var5_1)},
    {KM_PRESS | KB_E | SHIFT_LSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(var5_1)},
    {KM_PRESS | KB_E | SHIFT_RS, CONTEXT_ANY, KEYHANDLER_NAME(var5_1)},
    {KM_PRESS | KB_E | SHIFT_RSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(var5_1)},
    {KM_LPRESS | KB_E, CONTEXT_ANY, KEYHANDLER_NAME(var5_1)},
    {KM_KEYUP | KB_E, CONTEXT_ANY, KEYHANDLER_NAME(var5_1)},

    {KM_PRESS | KB_F, CONTEXT_ANY, KEYHANDLER_NAME(var6_1)},
    {KM_PRESS | KB_F | SHIFT_LS, CONTEXT_ANY, KEYHANDLER_NAME(var6_1)},
    {KM_PRESS | KB_F | SHIFT_LSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(var6_1)},
    {KM_PRESS | KB_F | SHIFT_RS, CONTEXT_ANY, KEYHANDLER_NAME(var6_1)},
    {KM_PRESS | KB_F | SHIFT_RSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(var6_1)},
    {KM_LPRESS | KB_F, CONTEXT_ANY, KEYHANDLER_NAME(var6_1)},
    {KM_KEYUP | KB_F, CONTEXT_ANY, KEYHANDLER_NAME(var6_1)},
#else /* TARGET_PRIME1 */

// VARS MENU KEYS - Variant #2 - only 6 keys for 1 menu

    {KM_PRESS | KB_SYM, CONTEXT_ANY, KEYHANDLER_NAME(var1_1)},
    {KM_PRESS | KB_SYM | SHIFT_LS, CONTEXT_ANY, KEYHANDLER_NAME(var1_1)},
    {KM_PRESS | KB_SYM | SHIFT_LSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(var1_1)},
    {KM_PRESS | KB_SYM | SHIFT_RS, CONTEXT_ANY, KEYHANDLER_NAME(var1_1)},
    {KM_PRESS | KB_SYM | SHIFT_RSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(var1_1)},
    {KM_LPRESS | KB_SYM, CONTEXT_ANY, KEYHANDLER_NAME(var1_1)},
    {KM_KEYUP | KB_SYM, CONTEXT_ANY, KEYHANDLER_NAME(var1_1)},

    {KM_PRESS | KB_PLT, CONTEXT_ANY, KEYHANDLER_NAME(var3_1)},
    {KM_PRESS | KB_PLT | SHIFT_LS, CONTEXT_ANY, KEYHANDLER_NAME(var3_1)},
    {KM_PRESS | KB_PLT | SHIFT_LSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(var3_1)},
    {KM_PRESS | KB_PLT | SHIFT_RS, CONTEXT_ANY, KEYHANDLER_NAME(var3_1)},
    {KM_PRESS | KB_PLT | SHIFT_RSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(var3_1)},
    {KM_LPRESS | KB_PLT, CONTEXT_ANY, KEYHANDLER_NAME(var3_1)},
    {KM_KEYUP | KB_PLT, CONTEXT_ANY, KEYHANDLER_NAME(var3_1)},

    {KM_PRESS | KB_NUM, CONTEXT_ANY, KEYHANDLER_NAME(var5_1)},
    {KM_PRESS | KB_NUM | SHIFT_LS, CONTEXT_ANY, KEYHANDLER_NAME(var5_1)},
    {KM_PRESS | KB_NUM | SHIFT_LSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(var5_1)},
    {KM_PRESS | KB_NUM | SHIFT_RS, CONTEXT_ANY, KEYHANDLER_NAME(var5_1)},
    {KM_PRESS | KB_NUM | SHIFT_RSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(var5_1)},
    {KM_LPRESS | KB_NUM, CONTEXT_ANY, KEYHANDLER_NAME(var5_1)},
    {KM_KEYUP | KB_NUM, CONTEXT_ANY, KEYHANDLER_NAME(var5_1)},

    {KM_PRESS | KB_HLP, CONTEXT_ANY, KEYHANDLER_NAME(var2_1)},
    {KM_PRESS | KB_HLP | SHIFT_LS, CONTEXT_ANY, KEYHANDLER_NAME(var2_1)},
    {KM_PRESS | KB_HLP | SHIFT_LSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(var2_1)},
    {KM_PRESS | KB_HLP | SHIFT_RS, CONTEXT_ANY, KEYHANDLER_NAME(var2_1)},
    {KM_PRESS | KB_HLP | SHIFT_RSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(var2_1)},
    {KM_LPRESS | KB_HLP, CONTEXT_ANY, KEYHANDLER_NAME(var2_1)},
    {KM_KEYUP | KB_HLP, CONTEXT_ANY, KEYHANDLER_NAME(var2_1)},

    {KM_PRESS | KB_VIE, CONTEXT_ANY, KEYHANDLER_NAME(var4_1)},
    {KM_PRESS | KB_VIE | SHIFT_LS, CONTEXT_ANY, KEYHANDLER_NAME(var4_1)},
    {KM_PRESS | KB_VIE | SHIFT_LSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(var4_1)},
    {KM_PRESS | KB_VIE | SHIFT_RS, CONTEXT_ANY, KEYHANDLER_NAME(var4_1)},
    {KM_PRESS | KB_VIE | SHIFT_RSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(var4_1)},
    {KM_LPRESS | KB_VIE, CONTEXT_ANY, KEYHANDLER_NAME(var4_1)},
    {KM_KEYUP | KB_VIE, CONTEXT_ANY, KEYHANDLER_NAME(var4_1)},

    {KM_PRESS | KB_MEN, CONTEXT_ANY, KEYHANDLER_NAME(var6_1)},
    {KM_PRESS | KB_MEN | SHIFT_LS, CONTEXT_ANY, KEYHANDLER_NAME(var6_1)},
    {KM_PRESS | KB_MEN | SHIFT_LSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(var6_1)},
    {KM_PRESS | KB_MEN | SHIFT_RS, CONTEXT_ANY, KEYHANDLER_NAME(var6_1)},
    {KM_PRESS | KB_MEN | SHIFT_RSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(var6_1)},
    {KM_LPRESS | KB_MEN, CONTEXT_ANY, KEYHANDLER_NAME(var6_1)},
    {KM_KEYUP | KB_MEN, CONTEXT_ANY, KEYHANDLER_NAME(var6_1)},

    {KM_PRESS | KB_CAS, CONTEXT_ANY, KEYHANDLER_NAME(switchmenukeys)},

#endif /* TARGET_PRIME1 */

// NORMAL COMMANDS/FUNCTIONS

#ifndef TARGET_PRIME1
    {KM_PRESS | KB_Y, CONTEXT_ANY, &invKeyHandler},
#else /* TARGET_PRIME1 */
    {KM_PRESS | KB_N, CONTEXT_ANY, &invKeyHandler},
#endif /* TARGET_PRIME1 */
    {KM_PRESS | KB_SPC, CONTEXT_ANY, &spcKeyHandler},
    {KM_REPEAT | KB_SPC, CONTEXT_ANY, &spcKeyHandler},
    {KM_PRESS | KB_SPC | SHIFT_ALPHA, CONTEXT_ANY, &spcKeyHandler},
    {KM_REPEAT | KB_SPC | SHIFT_ALPHA, CONTEXT_ANY, &spcKeyHandler},
    {KM_PRESS | KB_SPC | SHIFT_ALPHAHOLD, CONTEXT_ANY, &thinspcKeyHandler},
    {KM_REPEAT | KB_SPC | SHIFT_ALPHAHOLD, CONTEXT_ANY, &thinspcKeyHandler},
#ifndef TARGET_PRIME1
    {KM_PRESS | KB_W, CONTEXT_ANY, &chsKeyHandler},
    {KM_PRESS | KB_V, CONTEXT_ANY, &eexKeyHandler},
    {KM_PRESS | KB_ADD | SHIFT_LS, CONTEXT_ANY, &curlyBracketKeyHandler},
    {KM_PRESS | KB_ADD | SHIFT_LS | SHIFT_LSHOLD, CONTEXT_ANY, &curlyBracketKeyHandler},
#else /* TARGET_PRIME1 */
    {KM_PRESS | KB_M, CONTEXT_ANY, &chsKeyHandler},
    {KM_PRESS | KB_P, CONTEXT_ANY, &eexKeyHandler},
    {KM_PRESS | KB_8 | SHIFT_LS, CONTEXT_ANY, &curlyBracketKeyHandler},
    {KM_PRESS | KB_8 | SHIFT_LS | SHIFT_LSHOLD, CONTEXT_ANY, &curlyBracketKeyHandler},
#endif /* TARGET_PRIME1 */

#ifndef TARGET_PRIME1
    {KM_PRESS | KB_ADD | SHIFT_RS, CONTEXT_ANY, &secoBracketKeyHandler},
    {KM_PRESS | KB_ADD | SHIFT_RS | SHIFT_RSHOLD, CONTEXT_ANY, &secoBracketKeyHandler},
    {KM_PRESS | KB_SUB | SHIFT_LS, CONTEXT_ANY, &parenBracketKeyHandler},
    {KM_PRESS | KB_SUB | SHIFT_LS | SHIFT_LSHOLD, CONTEXT_ANY, &parenBracketKeyHandler},
    {KM_PRESS | KB_MUL | SHIFT_LS, CONTEXT_ANY, &squareBracketKeyHandler},
    {KM_PRESS | KB_MUL | SHIFT_LS | SHIFT_LSHOLD, CONTEXT_ANY, &squareBracketKeyHandler},
    {KM_PRESS | KB_MUL | SHIFT_RS, CONTEXT_ANY, &textBracketKeyHandler},
    {KM_PRESS | KB_MUL | SHIFT_RS | SHIFT_RSHOLD, CONTEXT_ANY, &textBracketKeyHandler},
    {KM_PRESS | KB_O, CONTEXT_ANY, &ticksKeyHandler},
    {KM_PRESS | KB_ADD | SHIFT_ALPHA | SHIFT_LS, CONTEXT_ANY, &curlyBracketKeyHandler},
    {KM_PRESS | KB_ADD | SHIFT_ALPHA | SHIFT_LS | SHIFT_LSHOLD, CONTEXT_ANY, &curlyBracketKeyHandler},
    {KM_PRESS | KB_ADD | SHIFT_ALPHA | SHIFT_RS, CONTEXT_ANY, &secoBracketKeyHandler},
    {KM_PRESS | KB_ADD | SHIFT_ALPHA | SHIFT_RS | SHIFT_RSHOLD, CONTEXT_ANY, &secoBracketKeyHandler},
#else /* TARGET_PRIME1 */
    {KM_PRESS | KB_8 | SHIFT_ALPHA | SHIFT_LS, CONTEXT_ANY, &curlyBracketKeyHandler},
    {KM_PRESS | KB_8 | SHIFT_ALPHA | SHIFT_LS | SHIFT_LSHOLD, CONTEXT_ANY, &curlyBracketKeyHandler},
    {KM_PRESS | KB_8 | SHIFT_RS, CONTEXT_ANY, &secoBracketKeyHandler},
    {KM_PRESS | KB_8 | SHIFT_RS | SHIFT_RSHOLD, CONTEXT_ANY, &secoBracketKeyHandler},
    {KM_PRESS | KB_5 | SHIFT_RS, CONTEXT_ANY, &secoBracketKeyHandler},
    {KM_PRESS | KB_5 | SHIFT_RS | SHIFT_RSHOLD, CONTEXT_ANY, &secoBracketKeyHandler},
    {KM_PRESS | KB_8 | SHIFT_ALPHA | SHIFT_RS, CONTEXT_ANY, &secoBracketKeyHandler},
    {KM_PRESS | KB_8 | SHIFT_ALPHA | SHIFT_RS | SHIFT_RSHOLD, CONTEXT_ANY, &secoBracketKeyHandler},
    {KM_PRESS | KB_5 | SHIFT_ALPHA | SHIFT_RS, CONTEXT_ANY, &secoBracketKeyHandler},
    {KM_PRESS | KB_5 | SHIFT_ALPHA | SHIFT_RS | SHIFT_RSHOLD, CONTEXT_ANY, &secoBracketKeyHandler},
#endif /* TARGET_PRIME1 */

#ifndef TARGET_PRIME1
    {KM_PRESS | KB_SUB | SHIFT_ALPHA | SHIFT_LS, CONTEXT_ANY, &parenBracketKeyHandler},
    {KM_PRESS | KB_SUB | SHIFT_ALPHA | SHIFT_LS | SHIFT_LSHOLD, CONTEXT_ANY, &parenBracketKeyHandler},
#else /* TARGET_PRIME1 */
    {KM_PRESS | KB_N | SHIFT_LS, CONTEXT_ANY, &parenBracketKeyHandler},
    {KM_PRESS | KB_N | SHIFT_LS | SHIFT_LSHOLD, CONTEXT_ANY, &parenBracketKeyHandler},
    {KM_PRESS | KB_N | SHIFT_ALPHA | SHIFT_LS, CONTEXT_ANY, &parenBracketKeyHandler},
    {KM_PRESS | KB_N | SHIFT_ALPHA | SHIFT_LS | SHIFT_LSHOLD, CONTEXT_ANY, &parenBracketKeyHandler},
#endif /* TARGET_PRIME1 */

#ifndef TARGET_PRIME1
    {KM_PRESS | KB_MUL | SHIFT_ALPHA | SHIFT_LS, CONTEXT_ANY, &squareBracketKeyHandler},
    {KM_PRESS | KB_MUL | SHIFT_ALPHA | SHIFT_LS | SHIFT_LSHOLD, CONTEXT_ANY, &squareBracketKeyHandler},
#else /* TARGET_PRIME1 */
    {KM_PRESS | KB_5 | SHIFT_LS, CONTEXT_ANY, &squareBracketKeyHandler},
    {KM_PRESS | KB_5 | SHIFT_LS | SHIFT_LSHOLD, CONTEXT_ANY, &squareBracketKeyHandler},
    {KM_PRESS | KB_5 | SHIFT_ALPHA | SHIFT_LS, CONTEXT_ANY, &squareBracketKeyHandler},
    {KM_PRESS | KB_5 | SHIFT_ALPHA | SHIFT_LS | SHIFT_LSHOLD, CONTEXT_ANY, &squareBracketKeyHandler},
#endif /* TARGET_PRIME1 */

#ifndef TARGET_PRIME1
    {KM_PRESS | KB_MUL | SHIFT_ALPHA | SHIFT_RS, CONTEXT_ANY, &textBracketKeyHandler},
    {KM_PRESS | KB_MUL | SHIFT_ALPHA | SHIFT_RS | SHIFT_RSHOLD, CONTEXT_ANY, &textBracketKeyHandler},
#else /* TARGET_PRIME1 */
    {KM_PRESS | KB_0 | SHIFT_RS, CONTEXT_ANY, &textBracketKeyHandler},
    {KM_PRESS | KB_0 | SHIFT_RS | SHIFT_RSHOLD, CONTEXT_ANY, &textBracketKeyHandler},
    {KM_PRESS | KB_0 | SHIFT_ALPHA | SHIFT_RS, CONTEXT_ANY, &textBracketKeyHandler},
    {KM_PRESS | KB_0 | SHIFT_ALPHA | SHIFT_RS | SHIFT_RSHOLD, CONTEXT_ANY, &textBracketKeyHandler},
#endif /* TARGET_PRIME1 */

#ifdef TARGET_PRIME1
    {KM_PRESS | KB_O, CONTEXT_ANY, &ticksKeyHandler},
#endif /* TARGET_PRIME1 */
    {KM_PRESS | KB_O | SHIFT_ALPHA | SHIFT_RS, CONTEXT_ANY, &ticksKeyHandler},
    {KM_PRESS | KB_O | SHIFT_ALPHA | SHIFT_RS | SHIFT_RSHOLD, CONTEXT_ANY, &ticksKeyHandler},

#ifndef TARGET_PRIME1
    {KM_PRESS | KB_DOT | SHIFT_LS, CONTEXT_ANY, &tagKeyHandler},
    {KM_PRESS | KB_DOT | SHIFT_LS | SHIFT_LSHOLD, CONTEXT_ANY, &tagKeyHandler},
    {KM_PRESS | KB_DOT | SHIFT_ALPHA | SHIFT_LS, CONTEXT_ANY, KEYHANDLER_NAME(colon)},
    {KM_PRESS | KB_DOT | SHIFT_ALPHA | SHIFT_LS | SHIFT_LSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(colon)},
#else /* TARGET_PRIME1 */
    {KM_PRESS | KB_SUB | SHIFT_RS, CONTEXT_ANY, &tagKeyHandler},
    {KM_PRESS | KB_SUB | SHIFT_RS | SHIFT_RSHOLD, CONTEXT_ANY, &tagKeyHandler},
    {KM_PRESS | KB_SUB | SHIFT_ALPHA | SHIFT_RS, CONTEXT_ANY, KEYHANDLER_NAME(colon)},
    {KM_PRESS | KB_SUB | SHIFT_ALPHA | SHIFT_RS | SHIFT_RSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(colon)},
#endif /* TARGET_PRIME1 */

    {KM_PRESS | KB_ADD | SHIFT_ONHOLD, CONTEXT_ANY, &onPlusKeyHandler},
    {KM_PRESS | KB_SUB | SHIFT_ONHOLD, CONTEXT_ANY, &onMinusKeyHandler},

    {KM_PRESS | KB_DOT | SHIFT_ONHOLD, CONTEXT_ANY, &onDotKeyHandler},
    {KM_PRESS | KB_SPC | SHIFT_ONHOLD, CONTEXT_ANY, &onSpcKeyHandler},
    {KM_PRESS | KB_MUL | SHIFT_ONHOLD, CONTEXT_ANY, &onMulDivKeyHandler},
#ifndef TARGET_PRIME1
    {KM_PRESS | KB_Z | SHIFT_ONHOLD, CONTEXT_ANY, &onMulDivKeyHandler},
#else /* TARGET_PRIME1 */
    {KM_PRESS | KB_DIV | SHIFT_ONHOLD, CONTEXT_ANY, &onMulDivKeyHandler},
#endif /* TARGET_PRIME1 */

    {KM_PRESS | KB_UP | SHIFT_ONHOLD, CONTEXT_ANY, &onUpDownKeyHandler},
    {KM_REPEAT | KB_UP | SHIFT_ONHOLD, CONTEXT_ANY, &onUpDownKeyHandler},
    {KM_PRESS | KB_DN | SHIFT_ONHOLD, CONTEXT_ANY, &onUpDownKeyHandler},
    {KM_REPEAT | KB_DN | SHIFT_ONHOLD, CONTEXT_ANY, &onUpDownKeyHandler},

    {KM_PRESS | KB_0 | SHIFT_ONHOLD, CONTEXT_ANY, &onDigitKeyHandler},
    {KM_PRESS | KB_1 | SHIFT_ONHOLD, CONTEXT_ANY, &onDigitKeyHandler},
    {KM_PRESS | KB_2 | SHIFT_ONHOLD, CONTEXT_ANY, &onDigitKeyHandler},
    {KM_PRESS | KB_3 | SHIFT_ONHOLD, CONTEXT_ANY, &onDigitKeyHandler},
    {KM_PRESS | KB_4 | SHIFT_ONHOLD, CONTEXT_ANY, &onDigitKeyHandler},
    {KM_PRESS | KB_5 | SHIFT_ONHOLD, CONTEXT_ANY, &onDigitKeyHandler},
    {KM_PRESS | KB_6 | SHIFT_ONHOLD, CONTEXT_ANY, &onDigitKeyHandler},
    {KM_PRESS | KB_7 | SHIFT_ONHOLD, CONTEXT_ANY, &onDigitKeyHandler},
    {KM_PRESS | KB_8 | SHIFT_ONHOLD, CONTEXT_ANY, &onDigitKeyHandler},
    {KM_PRESS | KB_9 | SHIFT_ONHOLD, CONTEXT_ANY, &onDigitKeyHandler},

#ifndef TARGET_PRIME1
    {KM_LPRESS | KB_J | SHIFT_ONHOLD, CONTEXT_ANY, &onVarKeyHandler},
    {KM_PRESS | KB_J | SHIFT_ONHOLD, CONTEXT_ANY, KEYHANDLER_NAME(menuswap)},
    {KM_PRESS | KB_B | SHIFT_ONHOLD, CONTEXT_ANY, &onBKeyHandler},
#endif /* ! TARGET_PRIME1 */

#ifndef TARGET_PRIME1
    {KM_PRESS | KB_0 | SHIFT_LS, CONTEXT_ANY, &infinityKeyHandler},
    {KM_PRESS | KB_0 | SHIFT_LS | SHIFT_LSHOLD, CONTEXT_ANY, &undinfinityKeyHandler},
#else /* TARGET_PRIME1 */
    { KM_PRESS  | KB_MEN | SHIFT_ONHOLD, CONTEXT_ANY, KEYHANDLER_NAME(menuswap)},
    { KM_LPRESS | KB_MEN | SHIFT_ONHOLD, CONTEXT_ANY, &onVarKeyHandler},
    { KM_PRESS  | KB_B | SHIFT_ONHOLD, CONTEXT_ANY, &onBKeyHandler},
#endif /* TARGET_PRIME1 */

#ifndef TARGET_PRIME1
    {KM_PRESS | KB_0 | SHIFT_LS | SHIFT_ALPHA, CONTEXT_ANY, &infinityKeyHandler},
    {KM_PRESS | KB_0 | SHIFT_LS | SHIFT_LSHOLD | SHIFT_ALPHA, CONTEXT_ANY, &undinfinityKeyHandler},
#else /* TARGET_PRIME1 */
    {KM_PRESS | KB_9 | SHIFT_LS | SHIFT_ALPHA, CONTEXT_ANY,&infinityKeyHandler},
    {KM_PRESS | KB_9 | SHIFT_LS, CONTEXT_ANY, &infinityKeyHandler},
    {KM_PRESS | KB_9 | SHIFT_LS | SHIFT_LSHOLD, CONTEXT_ANY, &undinfinityKeyHandler},
    {KM_PRESS | KB_9 | SHIFT_LS | SHIFT_LSHOLD | SHIFT_ALPHA, CONTEXT_ANY, &undinfinityKeyHandler},
#endif /* TARGET_PRIME1 */

#ifndef TARGET_PRIME1
    {KM_PRESS | KB_0 | SHIFT_RS, CONTEXT_ANY, &arrowKeyHandler},
    {KM_PRESS | KB_0 | SHIFT_RS | SHIFT_ALPHA, CONTEXT_ANY, &arrowKeyHandler},
    {KM_PRESS | KB_SPC | SHIFT_RS, CONTEXT_ANY, &commaKeyHandler},
    {KM_PRESS | KB_SPC | SHIFT_RS | SHIFT_ALPHA, CONTEXT_ANY, &commaKeyHandler},
    {KM_PRESS | KB_SPC | SHIFT_RS | SHIFT_RSHOLD, CONTEXT_ANY, &semiKeyHandler},
    {KM_PRESS | KB_SPC | SHIFT_RS | SHIFT_RSHOLD | SHIFT_ALPHA, CONTEXT_ANY, &semiKeyHandler},
#else /* TARGET_PRIME1 */
    {KM_PRESS | KB_SPC | SHIFT_RS, CONTEXT_ANY, &arrowKeyHandler},
    {KM_PRESS | KB_SPC | SHIFT_RS | SHIFT_ALPHA, CONTEXT_ANY, &arrowKeyHandler},
    {KM_PRESS | KB_SPC | SHIFT_RSHOLD, CONTEXT_ANY, &arrowKeyHandler},
    {KM_PRESS | KB_SPC | SHIFT_RSHOLD | SHIFT_ALPHA, CONTEXT_ANY, &arrowKeyHandler},

    {KM_PRESS | KB_O | SHIFT_RS, CONTEXT_ANY, &commaKeyHandler},
    {KM_PRESS | KB_O | SHIFT_RS | SHIFT_ALPHA, CONTEXT_ANY, &commaKeyHandler},

    {KM_PRESS | KB_ADD | SHIFT_RS , CONTEXT_ANY, &semiKeyHandler},
    {KM_PRESS | KB_ADD | SHIFT_RS | SHIFT_ALPHA, CONTEXT_ANY, &semiKeyHandler},
#endif /* TARGET_PRIME1 */

#ifndef TARGET_PRIME1
    {KM_PRESS | KB_SUB | SHIFT_RS, CONTEXT_ANY, &underscoreKeyHandler},
    {KM_PRESS | KB_SUB | SHIFT_RS | SHIFT_RSHOLD, CONTEXT_ANY, &underscoreKeyHandler},
    {KM_PRESS | KB_SUB | SHIFT_RS | SHIFT_ALPHA, CONTEXT_ANY, &underscoreKeyHandler},
#else /* TARGET_PRIME1 */
    {KM_PRESS | KB_SPC | SHIFT_LS, CONTEXT_ANY, &underscoreKeyHandler},
    {KM_PRESS | KB_SPC | SHIFT_LS | SHIFT_LSHOLD, CONTEXT_ANY, &underscoreKeyHandler},
    {KM_PRESS | KB_SPC | SHIFT_LS | SHIFT_ALPHA, CONTEXT_ANY, &underscoreKeyHandler},
#endif /* TARGET_PRIME1 */

#ifndef TARGET_PRIME1
    {KM_PRESS | KB_S, CONTEXT_ANY, &sinKeyHandler},
    {KM_PRESS | KB_T, CONTEXT_ANY, &cosKeyHandler},
    {KM_PRESS | KB_U, CONTEXT_ANY, &tanKeyHandler},
    {KM_PRESS | KB_S | SHIFT_LS, CONTEXT_ANY, &asinKeyHandler},
    {KM_PRESS | KB_T | SHIFT_LS, CONTEXT_ANY, &acosKeyHandler},
    {KM_PRESS | KB_U | SHIFT_LS, CONTEXT_ANY, &atanKeyHandler},
    {KM_PRESS | KB_S | SHIFT_LS | SHIFT_LSHOLD, CONTEXT_ANY, &asinKeyHandler},
    {KM_PRESS | KB_T | SHIFT_LS | SHIFT_LSHOLD, CONTEXT_ANY, &acosKeyHandler},
    {KM_PRESS | KB_U | SHIFT_LS | SHIFT_LSHOLD, CONTEXT_ANY, &atanKeyHandler},
    {KM_LPRESS | KB_S, CONTEXT_ANY, &sinhKeyHandler},
    {KM_LPRESS | KB_T, CONTEXT_ANY, &coshKeyHandler},
    {KM_LPRESS | KB_U, CONTEXT_ANY, &tanhKeyHandler},
    {KM_LPRESS | KB_S | SHIFT_LS, CONTEXT_ANY, &asinhKeyHandler},
    {KM_LPRESS | KB_T | SHIFT_LS, CONTEXT_ANY, &acoshKeyHandler},
    {KM_LPRESS | KB_U | SHIFT_LS, CONTEXT_ANY, &atanhKeyHandler},
    {KM_LPRESS | KB_S | SHIFT_LS | SHIFT_LSHOLD, CONTEXT_ANY, &asinhKeyHandler},
    {KM_LPRESS | KB_T | SHIFT_LS | SHIFT_LSHOLD, CONTEXT_ANY, &acoshKeyHandler},
    {KM_LPRESS | KB_U | SHIFT_LS | SHIFT_LSHOLD, CONTEXT_ANY, &atanhKeyHandler},

    {KM_PRESS | KB_N, CONTEXT_ANY, &evalKeyHandler},
    {KM_LPRESS | KB_N, CONTEXT_ANY, &eval1KeyHandler},
    {KM_PRESS | KB_ENT | SHIFT_RS, CONTEXT_ANY, &tonumKeyHandler},
    {KM_PRESS | KB_ENT | SHIFT_RS | SHIFT_RSHOLD, CONTEXT_ANY, &tonumKeyHandler},
#else /* TARGET_PRIME1 */
    {KM_PRESS | KB_G, CONTEXT_ANY, &sinKeyHandler},
    {KM_PRESS | KB_H, CONTEXT_ANY, &cosKeyHandler},
    {KM_PRESS | KB_I, CONTEXT_ANY, &tanKeyHandler},
    {KM_PRESS | KB_G | SHIFT_LS, CONTEXT_ANY, &asinKeyHandler},
    {KM_PRESS | KB_H | SHIFT_LS, CONTEXT_ANY, &acosKeyHandler},
    {KM_PRESS | KB_I | SHIFT_LS, CONTEXT_ANY, &atanKeyHandler},
    {KM_PRESS | KB_G | SHIFT_LS | SHIFT_LSHOLD, CONTEXT_ANY, &asinKeyHandler},
    {KM_PRESS | KB_H | SHIFT_LS | SHIFT_LSHOLD, CONTEXT_ANY, &acosKeyHandler},
    {KM_PRESS | KB_I | SHIFT_LS | SHIFT_LSHOLD, CONTEXT_ANY, &atanKeyHandler},
    {KM_LPRESS | KB_G, CONTEXT_ANY, &sinhKeyHandler},
    {KM_LPRESS | KB_H, CONTEXT_ANY, &coshKeyHandler},
    {KM_LPRESS | KB_I, CONTEXT_ANY, &tanhKeyHandler},
    {KM_LPRESS | KB_G | SHIFT_LS, CONTEXT_ANY, &asinhKeyHandler},
    {KM_LPRESS | KB_H | SHIFT_LS, CONTEXT_ANY, &acoshKeyHandler},
    {KM_LPRESS | KB_I | SHIFT_LS, CONTEXT_ANY, &atanhKeyHandler},
    {KM_LPRESS | KB_G | SHIFT_LS | SHIFT_LSHOLD, CONTEXT_ANY, &asinhKeyHandler},
    {KM_LPRESS | KB_H | SHIFT_LS | SHIFT_LSHOLD, CONTEXT_ANY, &acoshKeyHandler},
    {KM_LPRESS | KB_I | SHIFT_LS | SHIFT_LSHOLD, CONTEXT_ANY, &atanhKeyHandler},

    {KM_PRESS | KB_B, CONTEXT_ANY, &evalKeyHandler},
    {KM_LPRESS | KB_B, CONTEXT_ANY, &eval1KeyHandler},

    {KM_PRESS | KB_E | SHIFT_RS, CONTEXT_ANY, &tonumKeyHandler},
    {KM_PRESS | KB_E | SHIFT_RS | SHIFT_RSHOLD, CONTEXT_ANY, &tonumKeyHandler},
#endif /* TARGET_PRIME1 */

#ifndef TARGET_PRIME1
    {KM_PRESS | KB_ENT | SHIFT_LS, CONTEXT_ANY, KEYHANDLER_NAME(tofrac)},
    {KM_PRESS | KB_ENT | SHIFT_LS | SHIFT_LSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(tofrac)},

    {KM_PRESS | KB_R, CONTEXT_ANY, &sqrtKeyHandler},
    {KM_PRESS | KB_Q, CONTEXT_ANY, &powKeyHandler},
    {KM_PRESS | KB_Q | SHIFT_ALPHA | SHIFT_RS, CONTEXT_ANY, &powKeyHandler},
    {KM_PRESS | KB_Q | SHIFT_ALPHA | SHIFT_LS, CONTEXT_ANY, KEYHANDLER_NAME(econst)},
#else /* TARGET_PRIME1 */
    {KM_PRESS | KB_E , CONTEXT_ANY, KEYHANDLER_NAME(tofrac)},

    {KM_PRESS | KB_F, CONTEXT_ANY, &powKeyHandler},
    {KM_PRESS | KB_F | SHIFT_ALPHA | SHIFT_RS, CONTEXT_ANY, &powKeyHandler},
    {KM_PRESS | KB_F | SHIFT_LS, CONTEXT_ANY, KEYHANDLER_NAME(xroot)},
    {KM_PRESS | KB_F | SHIFT_LS | SHIFT_LSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(xroot)},

    {KM_PRESS | KB_J | SHIFT_ALPHA | SHIFT_LS, CONTEXT_ANY, KEYHANDLER_NAME(econst)},
#endif /* TARGET_PRIME1 */

#ifndef TARGET_PRIME1
    {KM_PRESS | KB_Q | SHIFT_ALPHA | SHIFT_LSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(econst)},
#else /* TARGET_PRIME1 */
    {KM_PRESS | KB_J | SHIFT_ALPHA | SHIFT_LSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(econst)},
#endif /* TARGET_PRIME1 */

#ifndef TARGET_PRIME1
    {KM_PRESS | KB_Q | SHIFT_LS, CONTEXT_ANY, KEYHANDLER_NAME(exp)},
    {KM_PRESS | KB_Q | SHIFT_LS | SHIFT_LSHOLD, CONTEXT_ANY,KEYHANDLER_NAME(exp)},
#else /* TARGET_PRIME1 */
    {KM_PRESS | KB_J | SHIFT_LS, CONTEXT_ANY, KEYHANDLER_NAME(exp)},
    {KM_PRESS | KB_J | SHIFT_LS | SHIFT_LSHOLD, CONTEXT_ANY,KEYHANDLER_NAME(exp)},
#endif /* TARGET_PRIME1 */

#ifndef TARGET_PRIME1
    {KM_PRESS | KB_Q | SHIFT_RS, CONTEXT_ANY, KEYHANDLER_NAME(ln)},
    {KM_PRESS | KB_Q | SHIFT_RS | SHIFT_RSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(ln)},

    {KM_PRESS | KB_M, CONTEXT_ANY, KEYHANDLER_NAME(sto)},
    {KM_LPRESS | KB_M, CONTEXT_ANY, KEYHANDLER_NAME(purge)},
    {KM_PRESS | KB_M | SHIFT_LS, CONTEXT_ANY, KEYHANDLER_NAME(rcl)},
    {KM_PRESS | KB_M | SHIFT_LS | SHIFT_LSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(rcl)},
#else /* TARGET_PRIME1 */
    {KM_PRESS | KB_J , CONTEXT_ANY, KEYHANDLER_NAME(ln)},
    {KM_PRESS | KB_J | SHIFT_RS , CONTEXT_ANY, KEYHANDLER_NAME(econst)},
    {KM_PRESS | KB_J | SHIFT_RS | SHIFT_RSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(econst)},


    {KM_PRESS | KB_A, CONTEXT_ANY, KEYHANDLER_NAME(sto)},
    {KM_LPRESS | KB_A, CONTEXT_ANY, KEYHANDLER_NAME(purge)},
    {KM_PRESS | KB_A | SHIFT_LS, CONTEXT_ANY, KEYHANDLER_NAME(rcl)},
    {KM_PRESS | KB_A | SHIFT_LS | SHIFT_LSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(rcl)},
#endif /* TARGET_PRIME1 */


#ifndef TARGET_PRIME1
    {KM_PRESS | KB_V | SHIFT_LS, CONTEXT_ANY, KEYHANDLER_NAME(alog)},
    {KM_PRESS | KB_V | SHIFT_LS | SHIFT_LSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(alog)},
#else /* TARGET_PRIME1 */

    {KM_PRESS | KB_K , CONTEXT_ANY, KEYHANDLER_NAME(log)},
    {KM_PRESS | KB_K | SHIFT_LS, CONTEXT_ANY, KEYHANDLER_NAME(alog)},
    {KM_PRESS | KB_K | SHIFT_LS | SHIFT_LSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(alog)},
#endif /* TARGET_PRIME1 */

#ifndef TARGET_PRIME1
    {KM_PRESS | KB_V | SHIFT_RS, CONTEXT_ANY, KEYHANDLER_NAME(log)},
    {KM_PRESS | KB_V | SHIFT_RS | SHIFT_RSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(log)},

    {KM_PRESS | KB_R | SHIFT_LS, CONTEXT_ANY, KEYHANDLER_NAME(sq)},
    {KM_PRESS | KB_R | SHIFT_LS | SHIFT_LSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(sq)},
    {KM_PRESS | KB_R | SHIFT_RS, CONTEXT_ANY, KEYHANDLER_NAME(xroot)},
    {KM_PRESS | KB_R | SHIFT_RS | SHIFT_RSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(xroot)},
#endif /* ! TARGET_PRIME1 */

#ifndef TARGET_PRIME1
    {KM_PRESS | KB_Z | SHIFT_LS, CONTEXT_ANY, KEYHANDLER_NAME(abs)},
    {KM_PRESS | KB_Z | SHIFT_LS | SHIFT_LSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(abs)},
#else /* TARGET_PRIME1 */
    {KM_PRESS | KB_L | SHIFT_LS, CONTEXT_ANY, &sqrtKeyHandler},
    {KM_PRESS | KB_L | SHIFT_LS | SHIFT_LSHOLD, CONTEXT_ANY, &sqrtKeyHandler},
    {KM_PRESS | KB_L , CONTEXT_ANY, KEYHANDLER_NAME(sq)},

    {KM_PRESS | KB_M | SHIFT_LS, CONTEXT_ANY, KEYHANDLER_NAME(abs)},
    {KM_PRESS | KB_M | SHIFT_LS | SHIFT_LSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(abs)},
#endif /* TARGET_PRIME1 */

#ifndef TARGET_PRIME1
    {KM_PRESS | KB_Z | SHIFT_RS, CONTEXT_ANY, KEYHANDLER_NAME(arg)},
    {KM_PRESS | KB_Z | SHIFT_RS | SHIFT_RSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(arg)},
#else /* TARGET_PRIME1 */

    {KM_PRESS | KB_M | SHIFT_RS, CONTEXT_ANY, KEYHANDLER_NAME(arg)},
    {KM_PRESS | KB_M | SHIFT_RS | SHIFT_RSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(arg)},
#endif /* TARGET_PRIME1 */


#ifdef TARGET_PRIME1
    // Directory navigation keys

#endif /* TARGET_PRIME1 */
    {KM_PRESS | KB_UP | SHIFT_LS, CONTEXT_ANY, KEYHANDLER_NAME(updir)},
    {KM_PRESS | KB_UP | SHIFT_LS | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(updir)},
    {KM_PRESS | KB_UP | SHIFT_LS | SHIFT_LSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(home)},
    {KM_PRESS | KB_UP | SHIFT_LS | SHIFT_LSHOLD | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(home)},

#ifndef TARGET_PRIME1
    {KM_PRESS | KB_X, CONTEXT_ANY, KEYHANDLER_NAME(keyx)},
#endif /* ! TARGET_PRIME1 */

#ifndef TARGET_PRIME1
    {KM_PRESS | KB_6 | SHIFT_LS, CONTEXT_ANY, KEYHANDLER_NAME(convert)},
    {KM_PRESS | KB_6 | SHIFT_LS | SHIFT_LSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(convert)},
#else /* TARGET_PRIME1 */
    {KM_PRESS | KB_D, CONTEXT_ANY, KEYHANDLER_NAME(keyx)},

    {KM_PRESS | KB_C | SHIFT_RS, CONTEXT_ANY, KEYHANDLER_NAME(convert)},
    {KM_PRESS | KB_C | SHIFT_RS | SHIFT_RSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(convert)},
#endif /* TARGET_PRIME1 */

#ifndef TARGET_PRIME1
    {KM_PRESS | KB_3 | SHIFT_RS | SHIFT_RSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(basecycle)},
#else /* TARGET_PRIME1 */
    {KM_PRESS | KB_SUB | SHIFT_LS | SHIFT_LSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(basecycle)},
#endif /* TARGET_PRIME1 */

#ifndef TARGET_PRIME1
    {KM_PRESS | KB_3 | SHIFT_RS | SHIFT_RSHOLD | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(basecycle)},
#else /* TARGET_PRIME1 */
    {KM_PRESS | KB_SUB | SHIFT_LS | SHIFT_LSHOLD | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(basecycle)},
#endif /* TARGET_PRIME1 */

// LETTERS
    {KM_PRESS | KB_A | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(a)},
    {KM_PRESS | KB_B | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(b)},
    {KM_PRESS | KB_C | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(c)},
    {KM_PRESS | KB_D | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(d)},
    {KM_PRESS | KB_E | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(e)},
    {KM_PRESS | KB_F | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(f)},
    {KM_PRESS | KB_G | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(g)},
    {KM_PRESS | KB_H | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(h)},
    {KM_PRESS | KB_I | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(i)},
    {KM_PRESS | KB_J | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(j)},
    {KM_PRESS | KB_K | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(k)},
    {KM_PRESS | KB_L | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(l)},
    {KM_PRESS | KB_M | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(m)},
    {KM_PRESS | KB_N | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(n)},
    {KM_PRESS | KB_O | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(o)},
    {KM_PRESS | KB_P | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(p)},
#ifndef TARGET_PRIME1
    {KM_PRESS | KB_Q | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(q)},
    {KM_PRESS | KB_R | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(r)},
    {KM_PRESS | KB_S | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(s)},
    {KM_PRESS | KB_T | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(t)},
    {KM_PRESS | KB_U | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(u)},
    {KM_PRESS | KB_V | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(v)},
    {KM_PRESS | KB_W | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(w)},
    {KM_PRESS | KB_X | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(x)},
    {KM_PRESS | KB_Y | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(y)},
    {KM_PRESS | KB_Q | SHIFT_ALPHAHOLD, CONTEXT_ANY, KEYHANDLER_NAME(q)},
    {KM_PRESS | KB_R | SHIFT_ALPHAHOLD, CONTEXT_ANY, KEYHANDLER_NAME(r)},
    {KM_PRESS | KB_S | SHIFT_ALPHAHOLD, CONTEXT_ANY, KEYHANDLER_NAME(s)},
    {KM_PRESS | KB_T | SHIFT_ALPHAHOLD, CONTEXT_ANY, KEYHANDLER_NAME(t)},
    {KM_PRESS | KB_U | SHIFT_ALPHAHOLD, CONTEXT_ANY, KEYHANDLER_NAME(u)},
    {KM_PRESS | KB_V | SHIFT_ALPHAHOLD, CONTEXT_ANY, KEYHANDLER_NAME(v)},
    {KM_PRESS | KB_W | SHIFT_ALPHAHOLD, CONTEXT_ANY, KEYHANDLER_NAME(w)},
    {KM_PRESS | KB_X | SHIFT_ALPHAHOLD, CONTEXT_ANY, KEYHANDLER_NAME(x)},
    {KM_PRESS | KB_Y | SHIFT_ALPHAHOLD, CONTEXT_ANY, KEYHANDLER_NAME(y)},
    {KM_PRESS | KB_DIV | SHIFT_ALPHAHOLD, CONTEXT_ANY, KEYHANDLER_NAME(z)},
#else /* TARGET_PRIME1 */
    {KM_PRESS | KB_HOM | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(q)},
    {KM_PRESS | KB_NUM | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(r)},
    {KM_PRESS | KB_MEN | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(s)},
    {KM_PRESS | KB_CAS | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(t)},
    {KM_PRESS | KB_APP | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(u)},
    {KM_PRESS | KB_PLT | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(v)},
    {KM_PRESS | KB_VIE | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(w)},
    {KM_PRESS | KB_SYM | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(x)},
    {KM_PRESS | KB_HLP | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(y)},
    {KM_PRESS | KB_HOM | SHIFT_ALPHA | SHIFT_ALPHAHOLD, CONTEXT_ANY, KEYHANDLER_NAME(q)},
    {KM_PRESS | KB_NUM | SHIFT_ALPHA | SHIFT_ALPHAHOLD, CONTEXT_ANY, KEYHANDLER_NAME(r)},
    {KM_PRESS | KB_MEN | SHIFT_ALPHA | SHIFT_ALPHAHOLD, CONTEXT_ANY, KEYHANDLER_NAME(s)},
    {KM_PRESS | KB_CAS | SHIFT_ALPHA | SHIFT_ALPHAHOLD, CONTEXT_ANY, KEYHANDLER_NAME(t)},
    {KM_PRESS | KB_APP | SHIFT_ALPHA | SHIFT_ALPHAHOLD, CONTEXT_ANY, KEYHANDLER_NAME(u)},
    {KM_PRESS | KB_PLT | SHIFT_ALPHA | SHIFT_ALPHAHOLD, CONTEXT_ANY, KEYHANDLER_NAME(v)},
    {KM_PRESS | KB_VIE | SHIFT_ALPHA | SHIFT_ALPHAHOLD, CONTEXT_ANY, KEYHANDLER_NAME(w)},
    {KM_PRESS | KB_SYM | SHIFT_ALPHA | SHIFT_ALPHAHOLD, CONTEXT_ANY, KEYHANDLER_NAME(x)},
    {KM_PRESS | KB_HLP | SHIFT_ALPHA | SHIFT_ALPHAHOLD, CONTEXT_ANY, KEYHANDLER_NAME(y)},
    {KM_PRESS | KB_DIV | SHIFT_ALPHA | SHIFT_ALPHAHOLD, CONTEXT_ANY, KEYHANDLER_NAME(z)},
#endif /* TARGET_PRIME1 */
    {KM_PRESS | KB_DIV | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(z)},
    {KM_PRESS | KB_A | SHIFT_ALPHAHOLD, CONTEXT_ANY, KEYHANDLER_NAME(a)},
    {KM_PRESS | KB_B | SHIFT_ALPHAHOLD, CONTEXT_ANY, KEYHANDLER_NAME(b)},
    {KM_PRESS | KB_C | SHIFT_ALPHAHOLD, CONTEXT_ANY, KEYHANDLER_NAME(c)},
    {KM_PRESS | KB_D | SHIFT_ALPHAHOLD, CONTEXT_ANY, KEYHANDLER_NAME(d)},
    {KM_PRESS | KB_E | SHIFT_ALPHAHOLD, CONTEXT_ANY, KEYHANDLER_NAME(e)},
    {KM_PRESS | KB_F | SHIFT_ALPHAHOLD, CONTEXT_ANY, KEYHANDLER_NAME(f)},
    {KM_PRESS | KB_G | SHIFT_ALPHAHOLD, CONTEXT_ANY, KEYHANDLER_NAME(g)},
    {KM_PRESS | KB_H | SHIFT_ALPHAHOLD, CONTEXT_ANY, KEYHANDLER_NAME(h)},
    {KM_PRESS | KB_I | SHIFT_ALPHAHOLD, CONTEXT_ANY, KEYHANDLER_NAME(i)},
    {KM_PRESS | KB_J | SHIFT_ALPHAHOLD, CONTEXT_ANY, KEYHANDLER_NAME(j)},
    {KM_PRESS | KB_K | SHIFT_ALPHAHOLD, CONTEXT_ANY, KEYHANDLER_NAME(k)},
    {KM_PRESS | KB_L | SHIFT_ALPHAHOLD, CONTEXT_ANY, KEYHANDLER_NAME(l)},
    {KM_PRESS | KB_M | SHIFT_ALPHAHOLD, CONTEXT_ANY, KEYHANDLER_NAME(m)},
    {KM_PRESS | KB_N | SHIFT_ALPHAHOLD, CONTEXT_ANY, KEYHANDLER_NAME(n)},
    {KM_PRESS | KB_O | SHIFT_ALPHAHOLD, CONTEXT_ANY, KEYHANDLER_NAME(o)},
    {KM_PRESS | KB_P | SHIFT_ALPHAHOLD, CONTEXT_ANY, KEYHANDLER_NAME(p)},

    {KM_PRESS | KB_ALPHA | SHIFT_ALPHAHOLD, CONTEXT_ANY, &alphaKeyHandler},


// SYMBOLS
#ifndef TARGET_PRIME1
    {KM_PRESS | KB_9 | SHIFT_RS | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(openquestion)},
#else /* TARGET_PRIME1 */
    {KM_PRESS | KB_7 | SHIFT_RS | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(question)},
    {KM_PRESS | KB_7 | SHIFT_RS | SHIFT_RSHOLD | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(openquestion)},
#endif /* TARGET_PRIME1 */

#ifndef TARGET_PRIME1
    {KM_PRESS | KB_9 | SHIFT_LS | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(delta)},
#else /* TARGET_PRIME1 */
    {KM_PRESS | KB_P | SHIFT_LS | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(delta)},
#endif /* TARGET_PRIME1 */

#ifndef TARGET_PRIME1
    {KM_PRESS | KB_9 | SHIFT_LS | SHIFT_LSHOLD | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(delta)},
#else /* TARGET_PRIME1 */
    {KM_PRESS | KB_P | SHIFT_LS | SHIFT_LSHOLD | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(delta)},
#endif /* TARGET_PRIME1 */

#ifndef TARGET_PRIME1
    {KM_PRESS | KB_8 | SHIFT_RS | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(openexclamation)},
    {KM_PRESS | KB_1 | SHIFT_RS | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(approx)},
#else /* TARGET_PRIME1 */
    {KM_PRESS | KB_ENT | SHIFT_LS | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(approx)},
#endif /* TARGET_PRIME1 */

    {KM_PRESS | KB_1 | SHIFT_LS | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(percent)},
#ifndef TARGET_PRIME1
    {KM_PRESS | KB_2 | SHIFT_LS | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(exclamation)},
    {KM_PRESS | KB_2 | SHIFT_LS | SHIFT_LSHOLD | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(exclamation)},
#else /* TARGET_PRIME1 */
    {KM_PRESS | KB_9 | SHIFT_RS | SHIFT_RSHOLD | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(openexclamation)},
    {KM_PRESS | KB_9 | SHIFT_RS | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(exclamation)},
#endif /* TARGET_PRIME1 */

#ifndef TARGET_PRIME1
    {KM_PRESS | KB_2 | SHIFT_LS, CONTEXT_ANY, KEYHANDLER_NAME(fact)},
    {KM_PRESS | KB_2 | SHIFT_LS | SHIFT_LSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(fact)},
#else /* TARGET_PRIME1 */
    {KM_PRESS | KB_9 | SHIFT_RS, CONTEXT_ANY, KEYHANDLER_NAME(fact)},
    {KM_PRESS | KB_9 | SHIFT_RS | SHIFT_RSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(fact)},
#endif /* TARGET_PRIME1 */

#ifndef TARGET_PRIME1
    {KM_PRESS | KB_3 | SHIFT_LS, CONTEXT_ANY, KEYHANDLER_NAME(hash)},
    {KM_PRESS | KB_3 | SHIFT_LS | SHIFT_LSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(hash)},
    {KM_PRESS | KB_3 | SHIFT_LS | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(hash)},
    {KM_PRESS | KB_3 | SHIFT_LS | SHIFT_LSHOLD | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(hash)},
#else /* TARGET_PRIME1 */
    {KM_PRESS | KB_3 | SHIFT_RS, CONTEXT_ANY, KEYHANDLER_NAME(hash)},
    {KM_PRESS | KB_3 | SHIFT_RS | SHIFT_RSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(hash)},
    {KM_PRESS | KB_3 | SHIFT_RS | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(hash)},
    {KM_PRESS | KB_3 | SHIFT_RS | SHIFT_RSHOLD | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(hash)},
#endif /* TARGET_PRIME1 */

#ifndef TARGET_PRIME1
    {KM_PRESS | KB_3 | SHIFT_RS | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(question)},
#endif /* TARGET_PRIME1 */
    {KM_PRESS | KB_4 | SHIFT_RS | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(euro)},
    {KM_PRESS | KB_4 | SHIFT_LS | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(dollar)},
#ifndef TARGET_PRIME1
    {KM_PRESS | KB_5 | SHIFT_LS | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(pound)},
#else /* TARGET_PRIME1 */
    {KM_PRESS | KB_4 | SHIFT_LS | SHIFT_LSHOLD | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(pound)},
#endif /* TARGET_PRIME1 */

#ifndef TARGET_PRIME1
    {KM_PRESS | KB_6 | SHIFT_RS | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(angle)},
#else /* TARGET_PRIME1 */
    {KM_PRESS | KB_MUL | SHIFT_LS | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(angle)},
    {KM_PRESS | KB_MUL | SHIFT_LS , CONTEXT_ANY, KEYHANDLER_NAME(angle)},
#endif /* TARGET_PRIME1 */

#ifndef TARGET_PRIME1
    {KM_PRESS | KB_6 | SHIFT_LS | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(degree)},
    {KM_PRESS | KB_6 | SHIFT_LS | SHIFT_LSHOLD | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(degree)},
#else /* TARGET_PRIME1 */
    {KM_PRESS | KB_MUL | SHIFT_LS | SHIFT_LSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(angle)},
    {KM_PRESS | KB_E | SHIFT_LS , CONTEXT_ANY, KEYHANDLER_NAME(degree)},
    {KM_PRESS | KB_E | SHIFT_LS | SHIFT_LSHOLD , CONTEXT_ANY, KEYHANDLER_NAME(degree)},
    {KM_PRESS | KB_E | SHIFT_LS | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(degree)},
    {KM_PRESS | KB_E | SHIFT_LS | SHIFT_LSHOLD | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(degree)},
#endif /* TARGET_PRIME1 */

#ifndef TARGET_PRIME1
    {KM_PRESS | KB_7 | SHIFT_LS | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(iconst)},
#else /* TARGET_PRIME1 */
    {KM_PRESS | KB_2 | SHIFT_LS , CONTEXT_ANY, KEYHANDLER_NAME(iconst)},
#endif /* TARGET_PRIME1 */

#ifndef TARGET_PRIME1
    {KM_PRESS | KB_7 | SHIFT_LS | SHIFT_LSHOLD | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(jconst)},
#else /* TARGET_PRIME1 */
    {KM_PRESS | KB_2 | SHIFT_LS | SHIFT_LSHOLD , CONTEXT_ANY, KEYHANDLER_NAME(jconst)},
    {KM_PRESS | KB_2 | SHIFT_LS | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(iconst)},
    {KM_PRESS | KB_2 | SHIFT_LS | SHIFT_LSHOLD | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(jconst)},
#endif /* TARGET_PRIME1 */

#ifndef TARGET_PRIME1
    {KM_PRESS | KB_SPC | SHIFT_LS, CONTEXT_ANY, KEYHANDLER_NAME(pi)},
    {KM_PRESS | KB_SPC | SHIFT_LS | SHIFT_LSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(pi)},
    {KM_PRESS | KB_SPC | SHIFT_LS | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(pi)},
    {KM_PRESS | KB_SPC | SHIFT_LS | SHIFT_LSHOLD | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(pi)},
#else /* TARGET_PRIME1 */
    {KM_PRESS | KB_3 | SHIFT_LS, CONTEXT_ANY, KEYHANDLER_NAME(pi)},
    {KM_PRESS | KB_3 | SHIFT_LS | SHIFT_LSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(pi)},
    {KM_PRESS | KB_3 | SHIFT_LS | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(pi)},
#endif /* TARGET_PRIME1 */

#ifndef TARGET_PRIME1
    {KM_PRESS | KB_ENT | SHIFT_LS | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(and)},
#else /* TARGET_PRIME1 */
    {KM_PRESS | KB_ADD | SHIFT_LS | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(and)},
#endif /* TARGET_PRIME1 */

#ifndef TARGET_PRIME1
    {KM_PRESS | KB_ENT | SHIFT_RS | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(at)},
#else /* TARGET_PRIME1 */
    {KM_PRESS | KB_ADD | SHIFT_RSHOLD | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(at)},
#endif /* TARGET_PRIME1 */

#ifndef TARGET_PRIME1
    {KM_PRESS | KB_ENT | SHIFT_LS | SHIFT_LSHOLD | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(and)},
    {KM_PRESS | KB_ENT | SHIFT_RS | SHIFT_RSHOLD | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(at)},
#else /* TARGET_PRIME1 */
    {KM_PRESS | KB_ADD | SHIFT_LS | SHIFT_LSHOLD | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(and)},
#endif /* TARGET_PRIME1 */

#ifndef TARGET_PRIME1
    {KM_PRESS | KB_W | SHIFT_RS, CONTEXT_ANY, KEYHANDLER_NAME(equal)},
    {KM_PRESS | KB_W | SHIFT_RS | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(equal)},
#else /* TARGET_PRIME1 */

    {KM_PRESS | KB_DOT | SHIFT_LS, CONTEXT_ANY, KEYHANDLER_NAME(equal)},
    {KM_PRESS | KB_DOT | SHIFT_LS | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(equal)},
#endif /* TARGET_PRIME1 */

#ifndef TARGET_PRIME1
    {KM_PRESS | KB_W | SHIFT_LS, CONTEXT_ANY, KEYHANDLER_NAME(notequal)},
    {KM_PRESS | KB_W | SHIFT_LS | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(notequal)},
#else /* TARGET_PRIME1 */
    {KM_PRESS | KB_DOT | SHIFT_RS, CONTEXT_ANY, KEYHANDLER_NAME(notequal)},
    {KM_PRESS | KB_DOT | SHIFT_RS | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(notequal)},
#endif /* TARGET_PRIME1 */

#ifndef TARGET_PRIME1
    {KM_PRESS | KB_X | SHIFT_RS, CONTEXT_ANY, KEYHANDLER_NAME(ls)},
    {KM_PRESS | KB_X | SHIFT_RS | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(ls)},
#else /* TARGET_PRIME1 */
    {KM_PRESS | KB_6 | SHIFT_LS, CONTEXT_ANY, KEYHANDLER_NAME(ls)},
    {KM_PRESS | KB_6 | SHIFT_LS | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(ls)},
#endif /* TARGET_PRIME1 */

#ifndef TARGET_PRIME1
    {KM_PRESS | KB_Y | SHIFT_RS, CONTEXT_ANY, KEYHANDLER_NAME(gt)},
    {KM_PRESS | KB_Y | SHIFT_RS | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(gt)},
#else /* TARGET_PRIME1 */
    {KM_PRESS | KB_6 | SHIFT_RS, CONTEXT_ANY, KEYHANDLER_NAME(gt)},
    {KM_PRESS | KB_6 | SHIFT_RS | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(gt)},
#endif /* TARGET_PRIME1 */

#ifndef TARGET_PRIME1
    {KM_PRESS | KB_X | SHIFT_LS, CONTEXT_ANY, KEYHANDLER_NAME(le)},
    {KM_PRESS | KB_X | SHIFT_LS | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(le)},
#else /* TARGET_PRIME1 */
    {KM_PRESS | KB_6 | SHIFT_LS | SHIFT_LSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(le)},
    {KM_PRESS | KB_6 | SHIFT_LS | SHIFT_LSHOLD | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(le)},
#endif /* TARGET_PRIME1 */

#ifndef TARGET_PRIME1
    {KM_PRESS | KB_Y | SHIFT_LS, CONTEXT_ANY, KEYHANDLER_NAME(ge)},
    {KM_PRESS | KB_Y | SHIFT_LS | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(ge)},
#else /* TARGET_PRIME1 */
    {KM_PRESS | KB_6 | SHIFT_RS | SHIFT_RSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(ge)},
    {KM_PRESS | KB_6 | SHIFT_RS | SHIFT_RSHOLD | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(ge)},
#endif /* TARGET_PRIME1 */

#ifndef TARGET_PRIME1
    {KM_PRESS | KB_DIV | SHIFT_ALPHA | SHIFT_RS, CONTEXT_ANY, KEYHANDLER_NAME(sdiv)},
    {KM_PRESS | KB_DIV | SHIFT_ALPHA | SHIFT_RSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(sdiv)},
    {KM_PRESS | KB_DIV | SHIFT_ALPHA | SHIFT_LS, CONTEXT_ANY, KEYHANDLER_NAME(backslash)},
    {KM_PRESS | KB_DIV | SHIFT_ALPHA | SHIFT_LSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(backslash)},
#else /* TARGET_PRIME1 */
    {KM_PRESS | KB_DIV | SHIFT_LS | SHIFT_ALPHA , CONTEXT_ANY, KEYHANDLER_NAME(backslash)},
    {KM_PRESS | KB_DIV | SHIFT_LSHOLD | SHIFT_ALPHA , CONTEXT_ANY, KEYHANDLER_NAME(backslash)},
#endif /* TARGET_PRIME1 */

#ifndef TARGET_PRIME1
    {KM_PRESS | KB_0 | SHIFT_RS | SHIFT_RSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(rulesep)},
#else /* TARGET_PRIME1 */
    {KM_PRESS | KB_SPC | SHIFT_RS | SHIFT_RSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(rulesep)},
#endif /* TARGET_PRIME1 */

#ifndef TARGET_PRIME1
    {KM_PRESS | KB_0 | SHIFT_ALPHA | SHIFT_RS | SHIFT_RSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(rulesep)},
#else /* TARGET_PRIME1 */
    {KM_PRESS | KB_SPC | SHIFT_ALPHA | SHIFT_RS | SHIFT_RSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(rulesep)},
#endif /* TARGET_PRIME1 */

    {KM_PRESS | KB_2 | SHIFT_ALPHA | SHIFT_RS | SHIFT_RSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(giventhat)},
    {KM_PRESS | KB_2 | SHIFT_ALPHA | SHIFT_RS, CONTEXT_ANY, KEYHANDLER_NAME(giventhat)},


// NUMBERS

    {KM_PRESS | KB_0 | SHIFT_ALPHAHOLD, CONTEXT_ANY, KEYHANDLER_NAME(sub0)},
    {KM_PRESS | KB_1 | SHIFT_ALPHAHOLD, CONTEXT_ANY, KEYHANDLER_NAME(sub1)},
    {KM_PRESS | KB_2 | SHIFT_ALPHAHOLD, CONTEXT_ANY, KEYHANDLER_NAME(sub2)},
    {KM_PRESS | KB_3 | SHIFT_ALPHAHOLD, CONTEXT_ANY, KEYHANDLER_NAME(sub3)},
    {KM_PRESS | KB_4 | SHIFT_ALPHAHOLD, CONTEXT_ANY, KEYHANDLER_NAME(sub4)},
    {KM_PRESS | KB_5 | SHIFT_ALPHAHOLD, CONTEXT_ANY, KEYHANDLER_NAME(sub5)},
    {KM_PRESS | KB_6 | SHIFT_ALPHAHOLD, CONTEXT_ANY, KEYHANDLER_NAME(sub6)},
    {KM_PRESS | KB_7 | SHIFT_ALPHAHOLD, CONTEXT_ANY, KEYHANDLER_NAME(sub7)},
    {KM_PRESS | KB_8 | SHIFT_ALPHAHOLD, CONTEXT_ANY, KEYHANDLER_NAME(sub8)},
    {KM_PRESS | KB_9 | SHIFT_ALPHAHOLD, CONTEXT_ANY, KEYHANDLER_NAME(sub9)},

// MENUS
#ifndef TARGET_PRIME1
    {KM_PRESS | KB_6 | SHIFT_RS, CONTEXT_ANY, KEYHANDLER_NAME(unitmenu)},
    {KM_PRESS | KB_N | SHIFT_LS, CONTEXT_ANY, KEYHANDLER_NAME(prgmenu)},
    {KM_PRESS | KB_P, CONTEXT_ANY, KEYHANDLER_NAME(mainmenu)},
    {KM_PRESS | KB_1 | SHIFT_LS, CONTEXT_ANY, KEYHANDLER_NAME(arithmenu)},
    {KM_PRESS | KB_1 | SHIFT_RS, CONTEXT_ANY, KEYHANDLER_NAME(cplxmenu)},
#else /* TARGET_PRIME1 */
    {KM_PRESS | KB_C | SHIFT_LS, CONTEXT_ANY, KEYHANDLER_NAME(unitmenu)},
    {KM_PRESS | KB_HOM, CONTEXT_ANY, KEYHANDLER_NAME(mainmenu)},
#endif /* TARGET_PRIME1 */
    {KM_PRESS | KB_2 | SHIFT_RS, CONTEXT_ANY, KEYHANDLER_NAME(libsmenu)},
#ifndef TARGET_PRIME1
    {KM_PRESS | KB_9 | SHIFT_RS, CONTEXT_ANY, KEYHANDLER_NAME(timemenu)},
    {KM_PRESS | KB_9 | SHIFT_LS, CONTEXT_ANY, KEYHANDLER_NAME(financemenu)},
    {KM_PRESS | KB_3 | SHIFT_RS, CONTEXT_ANY, KEYHANDLER_NAME(basemenu)},
#else /* TARGET_PRIME1 */
    {KM_PRESS | KB_HOM | SHIFT_RS, CONTEXT_ANY, KEYHANDLER_NAME(timemenu)},
    {KM_PRESS | KB_HOM | SHIFT_LS, CONTEXT_ANY, KEYHANDLER_NAME(financemenu)},
    {KM_PRESS | KB_HOM | SHIFT_LS | SHIFT_LSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(arithmenu)},
    {KM_PRESS | KB_HOM | SHIFT_RS | SHIFT_RSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(cplxmenu)},
    {KM_LPRESS | KB_HOM , CONTEXT_ANY, KEYHANDLER_NAME(prgmenu)},

    {KM_PRESS | KB_SUB | SHIFT_LS, CONTEXT_ANY, KEYHANDLER_NAME(basemenu)},
#endif /* TARGET_PRIME1 */
    {KM_PRESS | KB_7 | SHIFT_RS, CONTEXT_ANY, KEYHANDLER_NAME(numsolvermenu)},
#ifndef TARGET_PRIME1
    {KM_PRESS | KB_M | SHIFT_RS, CONTEXT_ANY, KEYHANDLER_NAME(backmenu1)},
    {KM_PRESS | KB_M | SHIFT_RS | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(backmenu1)},
#else /* TARGET_PRIME1 */
    {KM_PRESS | KB_A | SHIFT_RS, CONTEXT_ANY, KEYHANDLER_NAME(backmenu1)},
    {KM_PRESS | KB_A | SHIFT_RS | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(backmenu1)},
#endif /* TARGET_PRIME1 */

#ifndef TARGET_PRIME1
    {KM_PRESS | KB_M | SHIFT_RS | SHIFT_RSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(backmenu2)},
#else /* TARGET_PRIME1 */
    {KM_PRESS | KB_A | SHIFT_RS | SHIFT_RSHOLD, CONTEXT_ANY, KEYHANDLER_NAME(backmenu2)},
#endif /* TARGET_PRIME1 */

#ifndef TARGET_PRIME1
    {KM_PRESS | KB_M | SHIFT_RS | SHIFT_RSHOLD | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(backmenu2)},
#else /* TARGET_PRIME1 */
    {KM_PRESS | KB_A | SHIFT_RS | SHIFT_RSHOLD | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(backmenu2)},
#endif /* TARGET_PRIME1 */


// FORM SWITCHER
#ifndef TARGET_PRIME1
    {KM_LPRESS | KB_P, CONTEXT_ANY, KEYHANDLER_NAME(formswitcher)},
#else /* TARGET_PRIME1 */
    {KM_PRESS | KB_APP, CONTEXT_ANY, KEYHANDLER_NAME(formswitcher)},
#endif /* TARGET_PRIME1 */

// GREEK LETTERS

    {KM_PRESS | KB_A | SHIFT_LS | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(greekalpha)},
    {KM_PRESS | KB_A | SHIFT_LS | SHIFT_LSHOLD | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(greekalpha)},        // CAPITAL GREEK LETTERS ON SHIFT-HOLD PLANE

    {KM_PRESS | KB_B | SHIFT_LS | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(greekbeta)},
    {KM_PRESS | KB_B | SHIFT_LS | SHIFT_LSHOLD | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(greekbeta)}, // CAPITAL GREEK LETTERS ON SHIFT-HOLD PLANE

    {KM_PRESS | KB_C | SHIFT_LS | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(greekgamma)},
    {KM_PRESS | KB_C | SHIFT_LS | SHIFT_LSHOLD | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(greekgammacap)},     // CAPITAL GREEK LETTERS ON SHIFT-HOLD PLANE

    {KM_PRESS | KB_D | SHIFT_LS | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(greekdelta)},
    {KM_PRESS | KB_D | SHIFT_LS | SHIFT_LSHOLD | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(delta)},     // CAPITAL GREEK LETTERS ON SHIFT-HOLD PLANE

    {KM_PRESS | KB_E | SHIFT_LS | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(greekepsilon)},
    {KM_PRESS | KB_E | SHIFT_LS | SHIFT_LSHOLD | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(greekepsilon)},      // CAPITAL GREEK LETTERS ON SHIFT-HOLD PLANE

    {KM_PRESS | KB_F | SHIFT_LS | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(greekphi)},
    {KM_PRESS | KB_F | SHIFT_LS | SHIFT_LSHOLD | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(greekphicap)},       // CAPITAL GREEK LETTERS ON SHIFT-HOLD PLANE

    {KM_PRESS | KB_G | SHIFT_LS | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(greeketa)},
    {KM_PRESS | KB_G | SHIFT_LS | SHIFT_LSHOLD | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(greeketa)},  // CAPITAL GREEK LETTERS ON SHIFT-HOLD PLANE

    {KM_PRESS | KB_K | SHIFT_LS | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(greekkappa)},
    {KM_PRESS | KB_K | SHIFT_LS | SHIFT_LSHOLD | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(greekkappa)},        // CAPITAL GREEK LETTERS ON SHIFT-HOLD PLANE

    {KM_PRESS | KB_L | SHIFT_LS | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(greeklambda)},
    {KM_PRESS | KB_L | SHIFT_LS | SHIFT_LSHOLD | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(greeklambdacap)},    // CAPITAL GREEK LETTERS ON SHIFT-HOLD PLANE

    {KM_PRESS | KB_N | SHIFT_LS | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(greekmu)},
    {KM_PRESS | KB_N | SHIFT_LS | SHIFT_LSHOLD | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(greekmu)},   // CAPITAL GREEK LETTERS ON SHIFT-HOLD PLANE

    {KM_PRESS | KB_O | SHIFT_LS | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(greekomega)},
    {KM_PRESS | KB_O | SHIFT_LS | SHIFT_LSHOLD | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(greekomegacap)},     // CAPITAL GREEK LETTERS ON SHIFT-HOLD PLANE

#ifndef TARGET_PRIME1
    {KM_PRESS | KB_P | SHIFT_LS | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(pi)},
    {KM_PRESS | KB_P | SHIFT_LS | SHIFT_LSHOLD | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(greekpicap)},        // CAPITAL GREEK LETTERS ON SHIFT-HOLD PLANE
#else /* TARGET_PRIME1 */
    {KM_PRESS | KB_3 | SHIFT_LS | SHIFT_LSHOLD | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(greekpicap)},        // CAPITAL GREEK LETTERS ON SHIFT-HOLD PLANE
#endif /* TARGET_PRIME1 */

#ifndef TARGET_PRIME1
    {KM_PRESS | KB_R | SHIFT_LS | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(greekrho)},
#else /* TARGET_PRIME1 */
    {KM_PRESS | KB_HLP | SHIFT_LS | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(greekrho)},
#endif /* TARGET_PRIME1 */

#ifndef TARGET_PRIME1
    {KM_PRESS | KB_R | SHIFT_LS | SHIFT_LSHOLD | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(greekrho)},  // CAPITAL GREEK LETTERS ON SHIFT-HOLD PLANE
#else /* TARGET_PRIME1 */
    {KM_PRESS | KB_HLP | SHIFT_LS | SHIFT_LSHOLD | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(greekrho)},  // CAPITAL GREEK LETTERS ON SHIFT-HOLD PLANE
#endif /* TARGET_PRIME1 */

#ifndef TARGET_PRIME1
    {KM_PRESS | KB_S | SHIFT_LS | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(greeksigma)},
#else /* TARGET_PRIME1 */
    {KM_PRESS | KB_VIE | SHIFT_LS | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(greeksigma)},
#endif /* TARGET_PRIME1 */

#ifndef TARGET_PRIME1
    {KM_PRESS | KB_S | SHIFT_LS | SHIFT_LSHOLD | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(greeksigmacap)},     // CAPITAL GREEK LETTERS ON SHIFT-HOLD PLANE
#else /* TARGET_PRIME1 */
    {KM_PRESS | KB_VIE | SHIFT_LS | SHIFT_LSHOLD | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(greeksigmacap)},     // CAPITAL GREEK LETTERS ON SHIFT-HOLD PLANE
#endif /* TARGET_PRIME1 */

#ifndef TARGET_PRIME1
    {KM_PRESS | KB_T | SHIFT_LS | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(greektheta)},
#else /* TARGET_PRIME1 */
    {KM_PRESS | KB_MEN | SHIFT_LS | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(greektheta)},
#endif /* TARGET_PRIME1 */

#ifndef TARGET_PRIME1
    {KM_PRESS | KB_T | SHIFT_LS | SHIFT_LSHOLD | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(greekthetacap)},     // CAPITAL GREEK LETTERS ON SHIFT-HOLD PLANE
#else /* TARGET_PRIME1 */
    {KM_PRESS | KB_MEN | SHIFT_LS | SHIFT_LSHOLD | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(greekthetacap)},     // CAPITAL GREEK LETTERS ON SHIFT-HOLD PLANE
#endif /* TARGET_PRIME1 */

#ifndef TARGET_PRIME1
    {KM_PRESS | KB_U | SHIFT_LS | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(greektau)},
#else /* TARGET_PRIME1 */
    {KM_PRESS | KB_APP | SHIFT_LS | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(greektau)},
#endif /* TARGET_PRIME1 */

#ifndef TARGET_PRIME1
    {KM_PRESS | KB_U | SHIFT_LS | SHIFT_LSHOLD | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(greektau)},  // CAPITAL GREEK LETTERS ON SHIFT-HOLD PLANE
#else /* TARGET_PRIME1 */
    {KM_PRESS | KB_APP | SHIFT_LS | SHIFT_LSHOLD | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(greektau)},  // CAPITAL GREEK LETTERS ON SHIFT-HOLD PLANE
#endif /* TARGET_PRIME1 */

#ifndef TARGET_PRIME1
    {KM_PRESS | KB_V | SHIFT_LS | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(greeknu)},
#else /* TARGET_PRIME1 */
    {KM_PRESS | KB_HOM | SHIFT_LS | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(greeknu)},
#endif /* TARGET_PRIME1 */

#ifndef TARGET_PRIME1
    {KM_PRESS | KB_V | SHIFT_LS | SHIFT_LSHOLD | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(greeknu)},   // CAPITAL GREEK LETTERS ON SHIFT-HOLD PLANE

    {KM_PRESS | KB_T | SHIFT_LS | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(greektau)},
    {KM_PRESS | KB_T | SHIFT_LS | SHIFT_LSHOLD | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(greektau)},  // CAPITAL GREEK LETTERS ON SHIFT-HOLD PLANE
#else /* TARGET_PRIME1 */
    {KM_PRESS | KB_HOM | SHIFT_LS | SHIFT_LSHOLD | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(greeknu)},   // CAPITAL GREEK LETTERS ON SHIFT-HOLD PLANE
#endif /* TARGET_PRIME1 */

    {KM_PRESS | KB_M | SHIFT_LS | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(micro)},
    {KM_PRESS | KB_M | SHIFT_LS | SHIFT_LSHOLD | SHIFT_ALPHA, CONTEXT_ANY, KEYHANDLER_NAME(micro)},     // MICRO OR MICRON SYMBOL (SIMILAR TO GREEK LETTER MU)

    {0, 0, 0}
};

// DO CUSTOM KEYBOARD ACTIONS. RETURN 0 IF NO ACTION WAS DEFINED, NONZERO IF SOMETHING WAS EXECUTED
// KEY MESSAGES ARE PROCESSED THROUGH A LIST OF USER DEFINED KEYCODES
// { [KEYMESSAGE] [KEYCONTEXT] [ACTION] ... [KEYMESSAGE2] [KEYCONTEXT2] [ACTION2] ...}
// KEYS ARE IN NO PARTICULAR ORDER
// KEY TABLE IS SCANNED FROM START TO FINISH, NEW KEYS SHOULD BE ADDED TO THE HEAD
// OF THE LIST IN ORDER TO OVERRIDE PREVIOUS DEFINITIONS
// [KEYMESSAGE] AND [KEYCONTEXT] ARE BOTH SINT OBJECTS.
// [ACTION] IS AN ARBITRARY OBJECT THAT WILL BE XEQ'TED.
// HANDLER SCANS THE LIST, LOOKS FOR A MATCH IN KEYMESSAGE AND KEYCONTEXT.
// IF [KEYCONTEXT] IN THE TABLE IS 0, THEN ANY CONTEXT IS CONSIDERED A MATCH.
// ONCE A MATCH IS FOUND, THE [ACTION] OBJECT IS XEQ'TED.
// ONLY THE FIRST MATCH IS EXECUTED, THE SEARCH STOPS THERE.
// IF THE TABLE HAS NO MATCH, THE DEFAULT ACTION HANDLER IS CALLED.
// CUSTOM KEY LIST IS STORED IN Settings

// [ACTION] CAN BE:
// A PROGRAM: A NORMAL SECONDARY WILL BE EXECUTED USING uiCmdRun()
//            A :: ; SECONDARY WILL BE EXECUTED USING uiCmdRunTransparent()
// A LIST IN THE FORM { [COMMAND] }: THE COMMAND WILL BE EXECUTED USING cmdKeyHandler()
// A LIST IN THE FORM { [STRING] }:

int halDoCustomKey(WORD keymsg)
{
    if(rplTestSystemFlag(FL_NOCUSTOMKEYS))
        return 0;       // DON'T USE CUSTOM KEYS IF DISABLED PER FLAG

    // TODO: READ THE KEYBOARD TABLE FROM THE Settings DIRECTORY AND DO IT
    word_p keytable;

    keytable = rplGetSettings((word_p) customkey_ident);

    if(!keytable)
        return 0;       // NO CUSTOM KEY DEFINED

    if(!ISLIST(*keytable))
        return 0;       // INVALID KEY DEFINITION

    word_p ptr = keytable + 1, endoftable = rplSkipOb(keytable), action = 0;
    int32_t ctx, keepgoing, hanoffset;
    WORD msg;

    // CLEAR THE DEFAULT KEY FLAG, ANY OF THE CUSTOM HANDLERS CAN SET THIS FLAG TO HAVE THE DEFAULT KEY HANDLER EXECUTED
    rplClrSystemFlag(FL_DODEFAULTKEY);

    do {
        keepgoing = 0;
        while(ptr < endoftable) {
            msg = rplReadNumberAsInt64(ptr);
            if(Exceptions) {
                // CLEAR ALL ERRORS AND KEEP GOING
                rplClearErrors();
                return 0;
            }
            ptr = rplSkipOb(ptr);
            if(ptr >= endoftable)
                return 0;
            ctx = rplReadNumberAsInt64(ptr);
            if(Exceptions) {
                // CLEAR ALL ERRORS AND KEEP GOING
                rplClearErrors();
                return 0;
            }
            ptr = rplSkipOb(ptr);
            if(ptr >= endoftable)
                return 0;

            if(msg == keymsg) {
                if(ctx == 0) {
                    action = ptr;
                    break;
                }
                if(!(ctx & 0x1f)) {
                    if(ctx == (halScreen.KeyContext & ~0x1f)) {
                        action = ptr;
                        break;
                    }
                }
                else {
                    if(ctx == halScreen.KeyContext) {
                        action = ptr;
                        break;
                    }
                }
            }
            ptr = rplSkipOb(ptr);

        }

        if(action) {
            // EXECUTE THE REQUESTED ACTION
            // CLEAR THE NEXT HANDLER FLAGS, THE KEY HANDLER CAN SET THE FLAG TO CHAIN THE PREVIOUS HANDLER

            // REMEMBER WHICH HANDLER WAS EXECUTED

            hanoffset = action - keytable;

            rplClrSystemFlag(FL_DONEXTCUSTKEY);
            customKeyHandler(keymsg, action);

            if(rplTestSystemFlag(FL_DONEXTCUSTKEY) > 0) {

                // RESTORE ALL POINTERS, SINCE EXECUTION COULD'VE CHANGED EVERYTHING

                keytable = rplGetSettings((word_p) customkey_ident);

                if(!keytable)
                    return 1;   // NO MORE KEYS, KEYTABLE VANISHED?

                if(!ISLIST(*keytable))
                    return 1;   // INVALID KEY DEFINITION

                endoftable = rplSkipOb(keytable);
                action = 0;
                ptr = keytable + 1;
                while(ptr - keytable <= hanoffset) {
                    ptr = rplSkipOb(ptr);
                    if(ptr >= endoftable)
                        break;
                    ptr = rplSkipOb(ptr);
                    if(ptr >= endoftable)
                        break;
                    ptr = rplSkipOb(ptr);
                    if(ptr >= endoftable)
                        break;
                }

                // FOUND THE NEXT HANDLER TO SCAN OR THE END OF LIST

                keepgoing = 1;

            }
            else {
                if(rplTestSystemFlag(FL_DODEFAULTKEY) > 0)
                    halDoDefaultKey(keymsg);
                return 1;
            }

        }

    }
    while(keepgoing);

    return 0;
}

// RETURN TRUE/FALSE IF A CUSTOM HANDLER EXISTS
int halCustomKeyExists(WORD keymsg)
{
    word_p keytable;

    keytable = rplGetSettings((word_p) customkey_ident);

    if(!keytable)
        return 0;       // NO CUSTOM KEY DEFINED

    if(!ISLIST(*keytable))
        return 0;       // INVALID KEY DEFINITION

    word_p ptr = keytable + 1, endoftable = rplSkipOb(keytable);
    int32_t ctx;
    WORD msg;

    while(ptr < endoftable) {
        msg = rplReadNumberAsInt64(ptr);
        if(Exceptions) {
            // CLEAR ALL ERRORS AND KEEP GOING
            rplClearErrors();
            return 0;
        }
        ptr = rplSkipOb(ptr);
        if(ptr >= endoftable)
            return 0;
        ctx = rplReadNumberAsInt64(ptr);
        if(Exceptions) {
            // CLEAR ALL ERRORS AND KEEP GOING
            rplClearErrors();
            return 0;
        }
        if(msg == keymsg) {
            if(ctx == 0)
                return 1;
            if(!(ctx & 0x1f)) {
                if(ctx == (halScreen.KeyContext & ~0x1f))
                    return 1;
            }
            else {
                if(ctx == halScreen.KeyContext)
                    return 1;
            }
        }
        ptr = rplSkipOb(ptr);
        if(ptr >= endoftable)
            return 0;
        ptr = rplSkipOb(ptr);
    }

    //   SCANNED ENTIRE TABLE, NO LUCK
    return 0;
}

// CONTEXT MATCH FOR KEYS:
// IF CONTEXT == 0 (CONTEXT_ANY) IT'S CONSIDERED A MATCH TO ALL CONTEXTS/SUBCONTEXTS
// IF CONTEXT HAS A SUBCONTEXT, THEN IT ONLY MATCHES THE CONTEXT AND SUBCONTEXT EXACTLY
// IF CONTEXT DOESN'T HAVE A SUBCONTEXT (SUBCONTEXT_ANY), THEN IT MATCHES ALL SUBCONTEXTS WITHIN
// THE GIVEN MAIN CONTEXT.

int halDoDefaultKey(WORD keymsg)
{
    struct keyhandler_t *ptr = (struct keyhandler_t *)keydefaulthandlers;

    while(ptr->action) {
        if(ptr->message == keymsg) {
            // CHECK IF CONTEXT MATCHES
            if((!ptr->context) || (ptr->context == halScreen.KeyContext) ||
                    (!(ptr->context & 0x1f)
                        && (ptr->context == (halScreen.KeyContext & ~0x1f)))) {
                //  IT'S A MATCH, EXECUTE THE ACTION;
                (ptr->action) (keymsg);
                return 1;
            }
        }
        ++ptr;
    }
    return 0;
}

// RETURN TRUE/FALSE IF A DEFAULT HANDLER EXISTS
int halDefaultKeyExists(WORD keymsg)
{
    struct keyhandler_t *ptr = (struct keyhandler_t *)keydefaulthandlers;

    while(ptr->action) {
        if(ptr->message == keymsg) {
            // CHECK IF CONTEXT MATCHES
            if((!ptr->context) || (ptr->context == halScreen.KeyContext) ||
                    (!(ptr->context & 0x1f)
                        && (ptr->context == (halScreen.KeyContext & ~0x1f)))) {
                //  IT'S A MATCH;
                return 1;
            }
        }
        ++ptr;
    }
    return 0;

}

// PROCESSES KEY MESSAGES AND CALL APPROPRIATE HANDLERS BY KEYCODE

// RETURNS 0 IF THE LOOP HAS TO CONTINUE, 1 TO TERMINATE OUTER LOOP

int halProcessKey(WORD keymsg, int (*dokey)(WORD), int32_t flags)
{
    int wasProcessed;

    if(!keymsg)
        return 0;

#ifdef TARGET_PRIME1
    if(KM_MESSAGE(keymsg) == KM_TOUCH) {
        // TODO: Convert touch events into keys or gestures
        word_p keyname=rplMsg2KeyName(keymsg);
        if(keyname)
            rplPushData(keyname);
        else
            rplPushFalse();
    }
#endif /* TARGET_PRIME1 */

    if(KM_MESSAGE(keymsg) == KM_SHIFT) {
        halScreenUpdated();

        // THERE WAS A CHANGE IN SHIFT PLANE, UPDATE ANNUNCIATORS
        if(KM_SHIFTPLANE(keymsg) & SHIFT_LS) {
            if((KM_SHIFTPLANE(keymsg) & SHIFT_HOLD))
            {
                halSetNotification(N_LEFTSHIFT, ggl_color(PAL_GRAY15));
                halSetNotification(N_INTERNALSHIFTHOLD,ggl_color(PAL_GRAY15));
            }
            else
            {
                halSetNotification(N_LEFTSHIFT, ggl_color(PAL_GRAY8));
                halSetNotification(N_INTERNALSHIFTHOLD,ggl_color(PAL_GRAY0));
            }
        }
        else
        {
            halSetNotification(N_LEFTSHIFT, ggl_color(PAL_GRAY0));
            if(!(KM_SHIFTPLANE(keymsg) & SHIFT_HOLD))
                halSetNotification(N_INTERNALSHIFTHOLD,ggl_color(PAL_GRAY0));
        }

        if(KM_SHIFTPLANE(keymsg) & SHIFT_RS) {
            if((KM_SHIFTPLANE(keymsg) & SHIFT_HOLD))
            {
                halSetNotification(N_RIGHTSHIFT, ggl_color(PAL_GRAY15));
                halSetNotification(N_INTERNALSHIFTHOLD,ggl_color(PAL_GRAY15));
            }
            else
            {
                halSetNotification(N_RIGHTSHIFT, ggl_color(PAL_GRAY8));
                halSetNotification(N_INTERNALSHIFTHOLD,ggl_color(PAL_GRAY0));
            }
        }
        else
        {
            halSetNotification(N_RIGHTSHIFT, ggl_color(PAL_GRAY0));
            if(!(KM_SHIFTPLANE(keymsg) & SHIFT_HOLD))
                halSetNotification(N_INTERNALSHIFTHOLD,ggl_color(PAL_GRAY0));
        }
        if(KM_SHIFTPLANE(keymsg) & SHIFT_ALPHA) {
            if((KM_SHIFTPLANE(keymsg) & SHIFT_ALHOLD))
            {
                halSetNotification(N_ALPHA, ggl_color(PAL_GRAY15));
                halSetNotification(N_INTERNALALPHAHOLD,ggl_color(PAL_GRAY15));
            }
            else
            {
                halSetNotification(N_ALPHA, ggl_color(PAL_GRAY8));
                halSetNotification(N_INTERNALALPHAHOLD,ggl_color(PAL_GRAY0));
            }
        }
        else
        {
            halSetNotification(N_ALPHA, ggl_color(PAL_GRAY0));
            halSetNotification(N_INTERNALALPHAHOLD,ggl_color(PAL_GRAY0));
        }

        // UPDATE EDITOR MODE ACCORDINGLY
        int oldplane = OLDKEYSHIFT(keymsg);
        if(KM_SHIFTPLANE(keymsg ^ oldplane) & SHIFT_ALPHA) {
            // THERE WAS A CHANGE IN ALPHA MODE
            halSwapCmdLineMode(KM_SHIFTPLANE(keymsg) & SHIFT_ALPHA);
        }
        else {
            // NO CHANGE IN ALPHA STATE
            if(KM_SHIFTPLANE(oldplane) & SHIFT_ALPHALOCK) {
                if((KM_SHIFTPLANE(keymsg ^ oldplane) & SHIFT_ALPHAHOLD) ==
                        SHIFT_ALHOLD) {
                    // CHECK GOING FROM ALPHA TO ALPHA-HOLD OR VICEVERSA
                    // TEMPORARILY CHANGE SHIFT STATE
                    alphaKeyHandler(0);
                }
            }
        }

        return 0;

    }

    // THIS ALLOWS KEYS WITH LONG PRESS DEFINITION TO POSTPONE
    // EXECUTION UNTIL THE KEY IS RELEASED
    if(halLongKeyPending) {
        // THERE WAS A KEY PENDING EXECUTION
        if((KM_MESSAGE(keymsg) == KM_LPRESS)
                && (KM_KEY(keymsg) == KM_KEY(halLongKeyPending))) {
            // WE RECEIVED A LONG PRESS ON THAT KEY, DISCARD THE OLD EVENT AND DO A LONG PRESS ONLY
            halLongKeyPending = 0;
        }
        else {
            // ANY OTHER MESSAGE SHOULD CAUSE THE EXECUTION OF THE OLD KEY FIRST, THEN THE NEW ONE

            int32_t tmp = halLongKeyPending;
            halLongKeyPending = 0;      // THIS CLEANUP IS ONLY NEEDED IN CASE THE KEY HANDLER CALLS A KEYBOARD LOOP

            if(dokey)
                wasProcessed = (*dokey) (tmp);
            else
                wasProcessed = 0;

            if(!(flags & OL_NOCUSTOMKEYS))
                if(!wasProcessed)
                    wasProcessed = halDoCustomKey(tmp);
            if(!(flags & OL_NODEFAULTKEYS))
                if(!wasProcessed)
                    wasProcessed = halDoDefaultKey(tmp);

            if(wasProcessed < 0)
                return 1;       // DON'T EXECUTE THE NEW KEY IF THIS ONE WANTS TO END THE LOOP
        }

    }

    // BEFORE EXECUTING, CHECK IF THIS KEY HAS A LONG PRESS ASSIGNMENT
    // AND IF SO, DELAY EXECUTION

    if(KM_MESSAGE(keymsg) == KM_PRESS) {
        if(flags & OL_LONGPRESS) {
            // ALL KEYS WAIT FOR A LONG PRESS EVENT
            halLongKeyPending = keymsg;
            return 0;
        }
        else {
            // ONLY KEYS THAT HAVE LONG PRESS DEFINITION WILL WAIT, OTHERWISE EXECUTE IMMEDIATELY
            int32_t longmsg = KM_LPRESS | KM_SHIFTEDKEY(keymsg);

            if(halCustomKeyExists(longmsg)) {
                halLongKeyPending = keymsg;
                return 0;
            }
            if(halDefaultKeyExists(longmsg)) {
                halLongKeyPending = keymsg;
                return 0;
            }
        }
    }

    if(dokey)
        wasProcessed = (*dokey) (keymsg);
    else
        wasProcessed = 0;
    if(!(flags & OL_NOCUSTOMKEYS))
        if(!wasProcessed)
            wasProcessed = halDoCustomKey(keymsg);
    if(!(flags & OL_NODEFAULTKEYS))
        if(!wasProcessed)
            wasProcessed = halDoDefaultKey(keymsg);

#if 0
    // *************** DEBUG ONLY ************
    if (!wasProcessed &&
        ((KM_MESSAGE(keymsg) == KM_PRESS) || (KM_MESSAGE(keymsg) == KM_LPRESS) || (KM_MESSAGE(keymsg) == KM_REPEAT)))
    {
        // ALL OTHER KEYS, JUST DISPLAY THE KEY NAME ON SCREEN
        gglsurface scr;
        ggl_initscr(&scr);
        UNIFONT *fnt   = FONT_STATUS;

        // FOR DEBUG ONLY
        int      width = StringWidth((char *) keyNames[KM_KEY(keymsg)], fnt);
        int      ytop  = halScreen.Form + halScreen.Stack + halScreen.CmdLine + halScreen.Menu1;
        // CLEAR STATUS AREA AND SHOW KEY THERE
        ggl_rect(&scr, STATUS_AREA_X, ytop, LCD_W - 1, ytop + halScreen.Menu2 - 1, 0);
        DrawTextBk(LCD_W - width, ytop + halScreen.Menu2 / 2, (char *) keyNames[KM_KEY(keymsg)], fnt, 15, 0, &scr);
        char *shiftstr;
        switch (KM_SHIFTPLANE(keymsg))
        {
        case SHIFT_LS: shiftstr = "(LS)"; break;
        case SHIFT_LS | SHIFT_LSHOLD: shiftstr = "(LSH)"; break;
        case SHIFT_RS: shiftstr = "(RS)"; break;
        case SHIFT_RS | SHIFT_RSHOLD: shiftstr = "(RSH)"; break;
        case SHIFT_ALPHA: shiftstr = "(AL)"; break;
        case SHIFT_ALPHA | SHIFT_ALPHAHOLD: shiftstr = "(ALH)"; break;
        case SHIFT_ONHOLD: shiftstr = "(ONH)"; break;
        case SHIFT_ALPHA | SHIFT_LS: shiftstr = "(AL-LS)"; break;
        case SHIFT_ALPHA | SHIFT_RS: shiftstr = "(AL-RS)"; break;
        case SHIFT_ALPHA | SHIFT_LSHOLD: shiftstr = "(AL-LSH)"; break;
        case SHIFT_ALPHA | SHIFT_RSHOLD: shiftstr = "(AL-RSH)"; break;

        default: shiftstr = "";
        }
        DrawTextBk(LCD_W - width - 32, ytop + halScreen.Menu2 / 2, shiftstr, fnt, 15, 0, &scr);

        if (KM_MESSAGE(keymsg) == KM_LPRESS)
            DrawTextBk(LCD_W - width - 42, ytop + halScreen.Menu2 / 2, "L=", fnt, 15, 0, &scr);
    }
#  endif // DEBUG

    if(wasProcessed < 0)
        return 1;
    else
        return 0;
}

// SET A PROCESS TO BE EXECUTED AS SOON AS THERE'S NO MORE KEY PRESSES
void halDeferProcess(void (*function)(void))
{
    int k;
    for(k = 0; k < 3; ++k) {
        if(halProcesses[k] == 0) {
            halProcesses[k] = function;
            break;
        }
    }
}

// PERFORM ALL DEFERRED PROCESSES
void halDoDeferredProcess()
{
    int k;
    for(k = 0; k < 3; ++k) {
        if(halProcesses[k] != 0) {
            void (*func)() = halProcesses[k];
            halProcesses[k] = 0;
            (*func) ();
        }
    }

}

// THIS FUNCTION RETURNS WHEN THE FORM CLOSES, OR THE USER EXITS WITH THE ON KEY

void halOuterLoop(int32_t timeoutms, int (*dokey)(WORD), int (*doidle)(WORD), int32_t flags)
{
    WORD keymsg = 0;
    int isidle, jobdone;
    int64_t offcounter = 0;

    gglsurface scr;
    ggl_initscr(&scr);
    jobdone = isidle = 0;
    halTimeoutEvent = -1;

    do {
        if(!(halFlags & (HAL_RESET | HAL_HWRESET)))
            halRedrawAll(&scr);
        if(!(flags & OL_NOEXIT) && halExitOuterLoop())
            break;
        if(halFlags & HAL_POWEROFF) {
            halFlags &= ~HAL_POWEROFF;
#ifndef CONFIG_NO_FSYSTEM
            if (FSIsInit())
            {
                if (FSCardInserted())
                    FSShutdown();
                else
                    FSShutdownNoCard();
            }
#endif
            if(!(halFlags & (HAL_RESET | HAL_HWRESET))) {
                halPreparePowerOff();
                halEnterPowerOff();
            }
            else {
                if(!(halFlags & HAL_HWRESET)) {
                    //halFlags=0;   // DURING HAL RESET, DON'T PRESERVE THE FLAGS
                    //halPreparePowerOff(); // DON'T DO A POWEROFF PROCEDURE
                    halFlags = HAL_RESET;
                }
                else
                    halReset(); // THIS FUNCTION DOES NOT RETURN

            }
            return;
        }

        if(halFlags & HAL_FASTAUTORESUME) {
            halSetBusyHandler();
            jobdone = isidle = 0;
            halFlags &= ~HAL_FASTAUTORESUME;
            uiCmdRun(CMD_CONT); // AUTOMATICALLY CONTINUE EXECUTION BEFORE PROCESSING ANY KEYS
            halScreen.DirtyFlag |=
                    CMDLINE_ALLDIRTY | STACK_DIRTY | STAREA_DIRTY | MENU1_DIRTY
                    | MENU2_DIRTY | FORM_DIRTY;
            continue;
        }

        if(Exceptions) {
            if(flags & OL_EXITONERROR)
                break;
            halShowErrorMsg();
            Exceptions = 0;
        }

        keymsg = halWaitForKeyTimeout(timeoutms);
        timeoutms = 0;

        if (keymsg == 0xffffffff)
        {
            // TIMED OUT!
            if(halTimeoutEvent >= 0)
                tmr_eventkill(halTimeoutEvent);
            halTimeoutEvent = -1;
            halFlags &= ~HAL_TIMEOUT;
            break;      // JUST EXIT THE POL
        }

        if (!keymsg)
        {
            // SOMETHING OTHER THAN A KEY WOKE UP THE CPU

            halDoDeferredProcess();

            if(usb_isconfigured()) {
                halSetNotification(N_CONNECTION, ggl_color(PAL_GRAY15));
                if(usb_hasdata())
                    halSetNotification(N_DATARECVD, ggl_color(PAL_GRAY15));
                else
                    halSetNotification(N_DATARECVD, ggl_color(PAL_GRAY0));
            }
            else
                halSetNotification(N_CONNECTION, ggl_color(PAL_GRAY0));

            if(!(flags & OL_NOCOMMS)) {
                if(usb_hasdata()) {
                    if(!rplTestSystemFlag(FL_NOAUTORECV)) {
                        uiCmdRun(CMD_USBAUTORCV);
                        halScreen.DirtyFlag |=
                                CMDLINE_ALLDIRTY | STACK_DIRTY | STAREA_DIRTY |
                                MENU1_DIRTY | MENU2_DIRTY | FORM_DIRTY;
                        continue;
                    }
                }
            }

            if(!isidle)
                offcounter = halTicks();

#ifndef CONFIG_NO_FSYSTEM
            // FLUSH FILE SYSTEM CACHES WHEN IDLING FOR MORE THAN 3 SECONDS
            if(!(flags & OL_NOSDFLUSH) && !(jobdone & 1) && FSIsInit()) {

                if(halTicks() - offcounter >= 3000000) {
                    if(FSIsDirty()) {
                        FSFlushAll();
                        halUpdateStatus();
                    }
                    jobdone |= 1;
                    isidle = 0;
                }

            }
#endif

            // AUTO-OFF WHEN IDLING
            if(!(flags & OL_NOAUTOOFF) && (halFlags & HAL_AUTOOFFTIME)
                    && (!usb_isconnected())) {
                int64_t autoofftime = 15000000 << (GET_AUTOOFFTIME(halFlags));
                if(halTicks() - offcounter >= autoofftime) {
                    halPreparePowerOff();
                    halEnterPowerOff();
                }
            }

            if(!(flags & OL_NOALARM)) {
                if(halCheckSystemAlarm()) {
                    jobdone = isidle = 0;
                    halTriggerAlarm();
                }
            }

            // DO OTHER IDLE PROCESSING HERE

            if(halFlags & HAL_AUTORESUME) {
                halSetBusyHandler();
                jobdone = isidle = 0;
                uiCmdRun(CMD_CONT);     // AUTOMATICALLY CONTINUE EXECUTION AFTER 10 IDLE CYCLES
                halScreen.DirtyFlag |=
                        CMDLINE_ALLDIRTY | STACK_DIRTY | STAREA_DIRTY |
                        MENU1_DIRTY | MENU2_DIRTY | FORM_DIRTY;
                continue;
            }

            if(doidle) {
                if((*doidle) (0))
                    break;
            }

            isidle = 1;
        }
        else {
            jobdone = isidle = 0;
#ifdef TARGET_PRIME1
            // Reset count of the battery display so it only updates when idling for a while (Prime Only)
            extern int bat_readcnt;
            bat_readcnt=1;
#endif /* TARGET_PRIME1 */
        }

        halSetBusyHandler();

    }
    while(!halProcessKey(keymsg, dokey, flags));

    // MAKE SURE WE CLEANUP THE EVENT TIMER BEFORE WE EXIT SO IT DOESN'T TRIGGER INSIDE A PARENT POL
    if(halTimeoutEvent >= 0)
        tmr_eventkill(halTimeoutEvent);
    halTimeoutEvent = -1;
    halFlags &= ~HAL_TIMEOUT;
}

void halInitKeyboard()
{
    keyb_setalphalock(1);
    keyb_setshiftplane(0, 0, 0, 0);
#ifdef TARGET_PRIME1
    halKeyMenuSwitch=0;
#endif /* TARGET_PRIME1 */
}

// API USED BY RPL PROGRAMS TO INSERT KEY SEQUENCES TO THE KEYBOARD

void halPostKeyboardMessage(WORD keymsg)
{
    //  POST A COMPLETE KEY SEQUENCE TO PREVENT PROBLEMS.

    switch (KM_MESSAGE(keymsg)) {
    case KM_PRESS:
    {
        keyb_postmsg(KM_KEYDN | (keymsg ^ (KM_MESSAGE(keymsg))));
        keyb_postmsg(keymsg);
        keyb_postmsg(KM_KEYUP | (keymsg ^ (KM_MESSAGE(keymsg))));
        break;

    }
    case KM_LPRESS:
    {
        keyb_postmsg(KM_KEYDN | (keymsg ^ (KM_MESSAGE(keymsg))));
        keyb_postmsg(KM_PRESS | (keymsg ^ (KM_MESSAGE(keymsg))));
        keyb_postmsg(keymsg);
        keyb_postmsg(KM_KEYUP | (keymsg ^ (KM_MESSAGE(keymsg))));
        break;
    }
    default:
        keyb_postmsg(keymsg);
    }

}

#ifdef TARGET_PRIME1
void halTouch2KeyMsg()
{
    // TODO: Check for touch events and simulate key presses
}
#endif /* TARGET_PRIME1 */
