/****************************************************************************
**
**  This file is part of GAP, a system for computational discrete algebra.
**
**  Copyright of GAP belongs to its developers, whose names are too numerous
**  to list here. Please refer to the COPYRIGHT file for details.
**
**  SPDX-License-Identifier: GPL-2.0-or-later
**
**  This file contains the  various read-eval-print loops and streams related
**  stuff.  The system depend part is in "sysfiles.c".
*/

#include "streams.h"

#include "bool.h"
#include "calls.h"
#include "error.h"
#include "funcs.h"
#include "gap.h"
#include "gapstate.h"
#include "gaptime.h"
#include "gvars.h"
#include "integer.h"
#include "io.h"
#include "lists.h"
#include "modules.h"
#include "opers.h"
#include "plist.h"
#include "precord.h"
#include "read.h"
#include "records.h"
#include "stats.h"
#include "stringobj.h"
#include "sysfiles.h"
#include "sysopt.h"
#include "sysroots.h"
#include "sysstr.h"
#include "trycatch.h"
#include "vars.h"

#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#ifdef HAVE_SELECT
// For FuncUNIXSelect
#include <sys/time.h>
#endif


static Obj IsInputStream;
static Obj IsOutputStream;

#define RequireInputStream(funcname, op)                                     \
    RequireArgumentCondition(funcname, op,                                   \
                             CALL_1ARGS(IsInputStream, op) == True,          \
                             "must be an input stream")

#define RequireOutputStream(funcname, op)                                    \
    RequireArgumentCondition(funcname, op,                                   \
                             CALL_1ARGS(IsOutputStream, op) == True,         \
                             "must be an output stream")


/****************************************************************************
**
*F * * * * * * * * * streams and files related functions  * * * * * * * * * *
*/

static Int READ_COMMAND(Obj *evalResult)
{
    ExecStatus    status;

    ClearError();
    status = ReadEvalCommand(0, evalResult, 0);
    if( status == STATUS_EOF )
        return 0;

    if ( STATE(UserHasQuit) || STATE(UserHasQUIT) )
        return 0;
    
    /* handle return-value or return-void command                          */
    if ( status & (STATUS_RETURN_VAL | STATUS_RETURN_VOID) ) {
        Pr("'return' must not be used in file read-eval loop\n", 0, 0);
    }

    /* handle quit command                                 */
    else if (status == STATUS_QUIT) {
        SetRecursionDepth(0);
        STATE(UserHasQuit) = 1;
    }
    else if (status == STATUS_QQUIT) {
        STATE(UserHasQUIT) = 1;
    }
    ClearError();

    return 1;
}

/****************************************************************************
**
*F  FuncREAD_ALL_COMMANDS( <self>, <instream>, <echo>, <capture>, <outputFunc> )
**
**  FuncREAD_ALL_COMMANDS attempts to execute all statements read from the
**  stream <instream>. It returns 'fail' if the stream cannot be opened,
**  otherwise a list of lists, each entry of which reflects the result of the
**  execution of one statement.
**
**  If the parameter <echo> is 'true', then the statements are echoed to the
**  current output.
**
**  If the parameter <capture> is 'true', then any output occurring during
**  execution of a statement, including the output of <outputFunc>, is
**  captured into a string.
**
**  If <resultCallback> is a function, then this function is called on every
**  statement result, otherwise this parameter is ignored. Possible outputs of
**  this function are captured if <capture> is 'true'.
**
**  The results are returned as lists of length at most five, the structure of
**  which is explained below:
**
**  - The first entry is 'true' if the statement was executed successfully,
**    and 'false' otherwise.
**
**  - If the first entry is 'true', then the second entry is bound to the
**    result of the statement if there was one, and unbound otherwise.
**
**  - The third entry is 'true' if the statement ended in a dual semicolon,
**    and 'false' otherwise.
**
**  - The fourth entry contains the return value of <resultCallback> if
**    applicable.
**
**  - The fifth entry contains the captured output as a string, if <capture>
**    is 'true'.
**
**  This function is currently used in interactive tools such as the GAP
**  Jupyter kernel to execute cells and is likely to be replaced by a function
**  that can read a single command from a stream without losing the rest of
**  its content.
*/
Obj READ_ALL_COMMANDS(Obj instream, Obj echo, Obj capture, Obj resultCallback)
{
    ExecStatus status;
    UInt       dualSemicolon;
    Obj        result, resultList;
    Obj        copy;
    Obj        evalResult;
    Obj        outstream = 0;
    Obj        outstreamString = 0;

    RequireInputStream("READ_ALL_COMMANDS", instream);

    /* try to open the streams */
    if (!OpenInputStream(instream, echo == True)) {
        return Fail;
    }


    if (capture == True) {
        outstreamString = NEW_STRING(0);
        outstream = DoOperation2Args(ValGVar(GVarName("OutputTextString")),
                                     outstreamString, True);
    }
    if (outstream && !OpenOutputStream(outstream)) {
        CloseInput();
        return Fail;
    }

    resultList = NEW_PLIST(T_PLIST, 16);

    do {
        ClearError();
        if (outstream) {
            // Clean in case there has been any output
            SET_LEN_STRING(outstreamString, 0);
        }

        status = ReadEvalCommand(0, &evalResult, &dualSemicolon);

        if (!(status & (STATUS_EOF | STATUS_QUIT | STATUS_QQUIT))) {
            result = NEW_PLIST(T_PLIST, 5);
            AssPlist(result, 1, False);
            PushPlist(resultList, result);

            if (!(status & STATUS_ERROR)) {

                AssPlist(result, 1, True);
                AssPlist(result, 3, dualSemicolon ? True : False);

                if (evalResult) {
                    AssPlist(result, 2, evalResult);
                }

                if (evalResult && IS_FUNC(resultCallback) && !dualSemicolon) {
                    Obj tmp = CALL_1ARGS(resultCallback, evalResult);
                    AssPlist(result, 4, tmp);
                }
            }
            // Capture output
            if (capture == True) {
                // Flush output
                Pr("\03", 0, 0);
                copy = CopyToStringRep(outstreamString);
                SET_LEN_STRING(outstreamString, 0);
                AssPlist(result, 5, copy);
            }
        }
    } while (!(status & (STATUS_EOF | STATUS_QUIT | STATUS_QQUIT)));

    if (outstream)
        CloseOutput();
    CloseInput();
    ClearError();

    return resultList;
}

static Obj FuncREAD_ALL_COMMANDS(
    Obj self, Obj instream, Obj echo, Obj capture, Obj resultCallback)
{
    return READ_ALL_COMMANDS(instream, echo, capture, resultCallback);
}


/*
 Returns a list with one or two entries. The first
 entry is set to "false" if there was any error
 executing the command, and "true" otherwise.
 The second entry, if present, is the return value of
 the command. If it not present, the command returned nothing.
*/
static Obj FuncREAD_COMMAND_REAL(Obj self, Obj stream, Obj echo)
{
    Int status;
    Obj result;
    Obj evalResult;

    RequireInputStream(SELF_NAME, stream);

    result = NEW_PLIST( T_PLIST, 2 );
    SET_LEN_PLIST(result, 1);
    SET_ELM_PLIST(result, 1, False);

    /* try to open the file                                                */
    if (!OpenInputStream(stream, echo == True)) {
        return result;
    }

    status = READ_COMMAND(&evalResult);
    
    CloseInput();

    if( status == 0 ) return result;

    if (STATE(UserHasQUIT)) {
      STATE(UserHasQUIT) = 0;
      return result;
    }

    if (STATE(UserHasQuit)) {
      STATE(UserHasQuit) = 0;
    }
    
    SET_ELM_PLIST(result, 1, True);
    if (evalResult) {
        SET_LEN_PLIST(result, 2);
        SET_ELM_PLIST(result, 2, evalResult);
    }
    return result;
}

/****************************************************************************
**
*F  READ()  . . . . . . . . . . . . . . . . . . . . . . .  read current input
**
**  Read the current input and close the input stream.
*/

static UInt LastReadValueGVar;

static void READ_INNER(void)
{
    if (STATE(UserHasQuit))
      {
        Pr("Warning: Entering READ with UserHasQuit set, this should never happen, resetting",0,0);
        STATE(UserHasQuit) = 0;
      }
    if (STATE(UserHasQUIT))
      {
        Pr("Warning: Entering READ with UserHasQUIT set, this should never happen, resetting",0,0);
        STATE(UserHasQUIT) = 0;
      }
    AssGVarWithoutReadOnlyCheck( LastReadValueGVar, 0);
    /* now do the reading                                                  */
    while ( 1 ) {
        ClearError();
        Obj evalResult;
        ExecStatus status = ReadEvalCommand(0, &evalResult, 0);
        if (STATE(UserHasQuit) || STATE(UserHasQUIT))
            break;

        /* handle return-value or return-void command                      */
        if ( status & (STATUS_RETURN_VAL | STATUS_RETURN_VOID) ) {
            Pr("'return' must not be used in file read-eval loop\n", 0, 0);
        }

        /* handle quit command or <end-of-file>                            */
        else if ( status  & (STATUS_ERROR | STATUS_EOF)) 
          break;
        else if (status == STATUS_QUIT) {
          SetRecursionDepth(0);
          STATE(UserHasQuit) = 1;
          break;
        }
        else if (status == STATUS_QQUIT) {
          STATE(UserHasQUIT) = 1;
          break;
        }
        if (evalResult)
          {
            AssGVarWithoutReadOnlyCheck( LastReadValueGVar, evalResult);
          }
        
    }


    /* close the input file again, and return 'true'                       */
    if ( ! CloseInput() ) {
        ErrorQuit("Panic: READ cannot close input, this should not happen", 0,
                  0);
    }
    ClearError();
}


/****************************************************************************
**
*F  READ_AS_FUNC()  . . . . . . . . . . . . .  read current input as function
**
**  Read the current input as function and close the input stream.
*/
Obj READ_AS_FUNC ( void )
{
    /* now do the reading                                                  */
    ClearError();
    Obj evalResult;
    UInt type = ReadEvalFile(&evalResult);

    /* get the function                                                    */
    Obj func = (type == 0) ? evalResult : Fail;

    /* close the input file again, and return 'true'                       */
    if ( ! CloseInput() ) {
        ErrorQuit(
            "Panic: READ_AS_FUNC cannot close input, this should not happen",
            0, 0);
    }
    ClearError();

    /* return the function                                                 */
    return func;
}


static void READ_TEST_OR_LOOP(Obj context)
{
    UInt                type;
    UInt                oldtime;
    UInt                dualSemicolon;
    UInt                oldPrintObjState;

    /* get the starting time                                               */
    oldtime = SyTime();
    oldPrintObjState = SetPrintObjState(0);

    /* now do the reading                                                  */
    while ( 1 ) {

        /* read and evaluate the command                                   */
        SetPrintObjState(0);
        ClearError();
        Obj evalResult;
        type = ReadEvalCommand(context, &evalResult, &dualSemicolon);

        /* stop the stopwatch                                              */
        UpdateTime(oldtime);

        /* handle ordinary command                                         */
        if ( type == 0 && evalResult != 0 ) {

            /* remember the value in 'last' and the time in 'time'         */
            UpdateLast(evalResult, 3);

            /* print the result                                            */
            if ( ! dualSemicolon ) {
                ViewObjHandler( evalResult );
            }
        }

        /* handle return-value or return-void command                      */
        else if ( type & (STATUS_RETURN_VAL | STATUS_RETURN_VOID) ) {
            Pr("'return' must not be used in file read-eval loop\n", 0, 0);
        }

        /* handle quit command or <end-of-file>                            */
        else if ( type & (STATUS_QUIT | STATUS_QQUIT | STATUS_EOF) ) {
            break;
        }
        // FIXME: what about STATUS_ERROR

    }

    SetPrintObjState(oldPrintObjState);
    ClearError();
}


/****************************************************************************
**
*F  READ_GAP_ROOT( <filename> ) . . .  read from gap root, dyn-load or static
**
**  'READ_GAP_ROOT' tries to find  a file under  the root directory,  it will
**  search all   directories given   in 'SyGapRootPaths',  check  dynamically
**  loadable modules and statically linked modules.
*/
Int READ_GAP_ROOT ( const Char * filename )
{
    char path[GAP_PATH_MAX];

    // try to find the GAP file
    SyFindGapRootFile(filename, path, sizeof(path));

    // try to find compiled version of the GAP file
    if (SyUseModule) {
        // This code section covers transparently loading GAC compiled
        // versions of GAP source files, by running code similar to
        // that in FuncLOAD_STAT. For example, lib/oper1.g is compiled
        // into C code which is stored in src/c_oper1.c; when reading
        // lib/oper1.g, we instead will load its compiled version.
        Char module[GAP_PATH_MAX];
        strxcpy(module, "GAPROOT/", sizeof(module));
        strxcat(module, filename, sizeof(module));

        // search for a statically linked module matching the given filename
        StructInitInfo * info = LookupStaticModule(module);
        if (info) {
            // found a matching statically linked module; if there is also
            // a GAP file, compare their CRC
            if (*path && info->crc != SyGAPCRC(path)) {
                Pr("#W Static module %s has CRC mismatch, ignoring\n",
                   (Int)filename, 0);
            }
            else {
                if (SyDebugLoading) {
                    Pr("#I  READ_GAP_ROOT: loading '%s' statically\n",
                       (Int)filename, 0);
                }
                ActivateModule(info);
                RecordLoadedModule(info, 1, filename);
                return 1;
            }
        }
    }

    // not found?
    if (*path == 0)
        return 0;

#ifdef GAP_ENABLE_SAVELOAD
    // special handling case if we are trying to load compiled modules needed
    // for a saved workspace
    if (SyRestoring) {
        // ErrorQuit is not available
        Pr("Can't find compiled module '%s' needed by saved workspace\n",
           (Int)filename, 0);
        return 0;
    }
#endif

    // ordinary gap file
    if (SyDebugLoading) {
        Pr("#I  READ_GAP_ROOT: loading '%s' as GAP file\n", (Int)filename, 0);
    }
    if (OpenInput(path)) {
        while (1) {
            ClearError();
            UInt type = ReadEvalCommand(0, 0, 0);
            if (STATE(UserHasQuit) || STATE(UserHasQUIT))
                break;
            if (type & (STATUS_RETURN_VAL | STATUS_RETURN_VOID)) {
                Pr("'return' must not be used in file", 0, 0);
            }
            else if (type & (STATUS_QUIT | STATUS_EOF)) {
                break;
            }
        }
        CloseInput();
        ClearError();
        return 1;
    }

    return 0;
}


/****************************************************************************
**
*F  FuncCLOSE_LOG_TO()  . . . . . . . . . . . . . . . . . . . .  stop logging
**
**  'FuncCLOSE_LOG_TO' implements a method for 'LogTo'.
**
**  'LogTo()'
**
**  'LogTo' called with no argument closes the current logfile again, so that
**  input   from  '*stdin*'  and  '*errin*'  and  output  to  '*stdout*'  and
**  '*errout*' will no longer be echoed to a file.
*/
static Obj FuncCLOSE_LOG_TO(Obj self)
{
    if ( ! CloseLog() ) {
        ErrorQuit("LogTo: can not close the logfile", 0, 0);
    }
    return True;
}


/****************************************************************************
**
*F  FuncLOG_TO( <filename> ) . . . . . . . . . . . .  start logging to a file
**
**  'FuncLOG_TO' implements a method for 'LogTo'
**
**  'LogTo( <filename> )'
**
**  'LogTo' instructs GAP to echo all input from the  standard  input  files,
**  '*stdin*' and '*errin*' and all output  to  the  standard  output  files,
**  '*stdout*'  and  '*errout*',  to  the  file  with  the  name  <filename>.
**  The file is created if it does not  exist,  otherwise  it  is  truncated.
*/
static Obj FuncLOG_TO(Obj self, Obj filename)
{
    RequireStringRep(SELF_NAME, filename);
    if ( ! OpenLog( CONST_CSTR_STRING(filename) ) ) {
        ErrorReturnVoid("LogTo: cannot log to %g", (Int)filename, 0,
                        "you can 'return;'");
        return False;
    }
    return True;
}


/****************************************************************************
**
*F  FuncLOG_TO_STREAM( <stream> ) . . . . . . . . . start logging to a stream
*/
static Obj FuncLOG_TO_STREAM(Obj self, Obj stream)
{
    RequireOutputStream(SELF_NAME, stream);
    if ( ! OpenLogStream(stream) ) {
        ErrorReturnVoid("LogTo: cannot log to stream", 0, 0,
                        "you can 'return;'");
        return False;
    }
    return True;
}


/****************************************************************************
**
*F  FuncCLOSE_INPUT_LOG_TO()  . . . . . . . . . . . . . . . . .  stop logging
**
**  'FuncCLOSE_INPUT_LOG_TO' implements a method for 'InputLogTo'.
**
**  'InputLogTo()'
**
**  'InputLogTo' called with no argument closes the current logfile again, so
**  that input from  '*stdin*' and '*errin*' will   no longer be  echoed to a
**  file.
*/
static Obj FuncCLOSE_INPUT_LOG_TO(Obj self)
{
    if ( ! CloseInputLog() ) {
        ErrorQuit("InputLogTo: can not close the logfile", 0, 0);
    }
    return True;
}


/****************************************************************************
**
*F  FuncINPUT_LOG_TO( <filename> )  . . . . . . . . . start logging to a file
**
**  'FuncINPUT_LOG_TO' implements a method for 'InputLogTo'
**
**  'InputLogTo( <filename> )'
**
**  'InputLogTo'  instructs  GAP to echo   all input from  the standard input
**  files, '*stdin*' and '*errin*' to the file with the name <filename>.  The
**  file is created if it does not exist, otherwise it is truncated.
*/
static Obj FuncINPUT_LOG_TO(Obj self, Obj filename)
{
    RequireStringRep(SELF_NAME, filename);
    if ( ! OpenInputLog( CONST_CSTR_STRING(filename) ) ) {
        ErrorReturnVoid("InputLogTo: cannot log to %g", (Int)filename, 0,
                        "you can 'return;'");
        return False;
    }
    return True;
}


/****************************************************************************
**
*F  FuncINPUT_LOG_TO_STREAM( <stream> ) . . . . . . start logging to a stream
*/
static Obj FuncINPUT_LOG_TO_STREAM(Obj self, Obj stream)
{
    RequireOutputStream(SELF_NAME, stream);
    if ( ! OpenInputLogStream(stream) ) {
        ErrorReturnVoid("InputLogTo: cannot log to stream", 0, 0,
                        "you can 'return;'");
        return False;
    }
    return True;
}


/****************************************************************************
**
*F  FuncCLOSE_OUTPUT_LOG_TO()  . . . . . . . . . . . . . . . . . stop logging
**
**  'FuncCLOSE_OUTPUT_LOG_TO' implements a method for 'OutputLogTo'.
**
**  'OutputLogTo()'
**
**  'OutputLogTo'  called with no argument  closes the current logfile again,
**  so that output from '*stdin*' and '*errin*' will no longer be echoed to a
**  file.
*/
static Obj FuncCLOSE_OUTPUT_LOG_TO(Obj self)
{
    if ( ! CloseOutputLog() ) {
        ErrorQuit("OutputLogTo: can not close the logfile", 0, 0);
    }
    return True;
}


/****************************************************************************
**
*F  FuncOUTPUT_LOG_TO( <filename> )  . . . . . . . .  start logging to a file
**
**  'FuncOUTPUT_LOG_TO' implements a method for 'OutputLogTo'
**
**  'OutputLogTo( <filename> )'
**
**  'OutputLogTo' instructs GAP  to echo all  output from the standard output
**  files, '*stdin*' and '*errin*' to the file with the name <filename>.  The
**  file is created if it does not exist, otherwise it is truncated.
*/
static Obj FuncOUTPUT_LOG_TO(Obj self, Obj filename)
{
    RequireStringRep(SELF_NAME, filename);
    if ( ! OpenOutputLog( CONST_CSTR_STRING(filename) ) ) {
        ErrorReturnVoid("OutputLogTo: cannot log to %g", (Int)filename, 0,
                        "you can 'return;'");
        return False;
    }
    return True;
}


/****************************************************************************
**
*F  FuncOUTPUT_LOG_TO_STREAM( <stream> ) . . . . .  start logging to a stream
*/
static Obj FuncOUTPUT_LOG_TO_STREAM(Obj self, Obj stream)
{
    RequireOutputStream(SELF_NAME, stream);
    if ( ! OpenOutputLogStream(stream) ) {
        ErrorReturnVoid("OutputLogTo: cannot log to stream", 0, 0,
                        "you can 'return;'");
        return False;
    }
    return True;
}


/****************************************************************************
**
*F  FuncPrint( <self>, <args> ) . . . . . . . . . . . . . . . .  print <args>
*/
static Obj FuncPrint(Obj self, Obj args)
{
    volatile Obj        arg;
    volatile UInt       i;

    /* print all the arguments, take care of strings and functions         */
    for ( i = 1;  i <= LEN_PLIST(args);  i++ ) {
        arg = ELM_LIST(args,i);
        if ( IS_PLIST(arg) && 0 < LEN_PLIST(arg) && IsStringConv(arg) ) {
            PrintString1(arg);
        }
        else if ( IS_STRING_REP(arg) ) {
            PrintString1(arg);
        }
        else if ( TNUM_OBJ( arg ) == T_FUNCTION ) {
            PrintFunction( arg );
        }
        else {
            PrintObj( arg );
        }
    }

    return 0;
}

static Obj PRINT_OR_APPEND_TO_FILE_OR_STREAM(Obj args, int append, int file)
{
    const char * volatile funcname = append ? "AppendTo" : "PrintTo";
    volatile Obj        arg;
    volatile Obj        destination;
    volatile UInt       i;

    /* first entry is the file or stream                                   */
    destination = ELM_LIST(args, 1);

    /* try to open the output and handle failures                          */
    if (file) {
        RequireStringRep(funcname, destination);
        i = append ? OpenAppend(CONST_CSTR_STRING(destination))
                   : OpenOutput(CONST_CSTR_STRING(destination));
        if (!i) {
            if (strcmp(CSTR_STRING(destination), "*errout*") == 0) {
                Panic("Failed to open *errout*!");
            }
            ErrorQuit("%s: cannot open '%g' for output", (Int)funcname,
                      (Int)destination);
        }
    }
    else {
        if (CALL_1ARGS(IsOutputStream, destination) != True) {
            ErrorQuit("%s: <outstream> must be an output stream",
                      (Int)funcname, 0);
        }
        i = OpenOutputStream(destination);
        if (!i) {
            ErrorQuit("%s: cannot open stream for output", (Int)funcname, 0);
        }
    }

    /* print all the arguments, take care of strings and functions         */
    for ( i = 2;  i <= LEN_PLIST(args);  i++ ) {
        arg = ELM_LIST(args,i);

        /* if an error occurs stop printing                                */
        GAP_TRY
        {
            if (IS_PLIST(arg) && 0 < LEN_PLIST(arg) && IsStringConv(arg)) {
                PrintString1(arg);
            }
            else if (IS_STRING_REP(arg)) {
                PrintString1(arg);
            }
            else if (IS_FUNC(arg)) {
                PrintFunction(arg);
            }
            else {
                PrintObj(arg);
            }
        }
        GAP_CATCH
        {
            CloseOutput();
            GAP_THROW();
        }
    }

    /* close the output file again, and return nothing                     */
    if ( ! CloseOutput() ) {
        ErrorQuit("%s: cannot close output", (Int)funcname, 0);
    }

    return 0;
}
static Obj PRINT_OR_APPEND_TO(Obj args, int append)
{
    return PRINT_OR_APPEND_TO_FILE_OR_STREAM(args, append, 1);
}


static Obj PRINT_OR_APPEND_TO_STREAM(Obj args, int append)
{
    return PRINT_OR_APPEND_TO_FILE_OR_STREAM(args, append, 0);
}

/****************************************************************************
**
*F  FuncPRINT_TO( <self>, <args> )  . . . . . . . . . . . . . .  print <args>
*/
static Obj FuncPRINT_TO(Obj self, Obj args)
{
    return PRINT_OR_APPEND_TO(args, 0);
}


/****************************************************************************
**
*F  FuncPRINT_TO_STREAM( <self>, <args> ) . . . . . . . . . . .  print <args>
*/
static Obj FuncPRINT_TO_STREAM(Obj self, Obj args)
{
    /* Note that FuncPRINT_TO_STREAM and FuncAPPEND_TO_STREAM do exactly the
       same, they only differ in the function name they print as part
       of their error messages. */
    return PRINT_OR_APPEND_TO_STREAM(args, 0);
}


/****************************************************************************
**
*F  FuncAPPEND_TO( <self>, <args> ) . . . . . . . . . . . . . . append <args>
*/
static Obj FuncAPPEND_TO(Obj self, Obj args)
{
    return PRINT_OR_APPEND_TO(args, 1);
}


/****************************************************************************
**
*F  FuncAPPEND_TO_STREAM( <self>, <args> )  . . . . . . . . . . append <args>
*/
static Obj FuncAPPEND_TO_STREAM(Obj self, Obj args)
{
    /* Note that FuncPRINT_TO_STREAM and FuncAPPEND_TO_STREAM do exactly the
       same, they only differ in the function name they print as part
       of their error messages. */
    return PRINT_OR_APPEND_TO_STREAM(args, 1);
}


/****************************************************************************
**
*F  FuncREAD( <self>, <filename> )  . . . . . . . . . . . . . . . read a file
**
**  Read the current input and close the input stream.
*/
static Obj FuncREAD(Obj self, Obj filename)
{
    RequireStringRep(SELF_NAME, filename);

    /* try to open the file                                                */
    if ( ! OpenInput( CONST_CSTR_STRING(filename) ) ) {
        return False;
    }

    /* read the test file                                                  */
    READ_INNER();
    return True;
}

/****************************************************************************
**
*F  FuncREAD_NORECOVERY( <self>, <filename> )  . . .  . . . . . . read a file
**
**  Read the current input and close the input stream. Disable the normal 
**  mechanism which ensures that quitting from a break loop gets you back to
**  a live prompt. This is initially designed for the files read from the
**  command line.
*/
static Obj FuncREAD_NORECOVERY(Obj self, Obj input)
{
    if ( IsStringConv( input ) ) {
        if ( ! OpenInput( CONST_CSTR_STRING(input) ) ) {
            return False;
        }
    }
    else if (CALL_1ARGS(IsInputStream, input) == True) {
        if (!OpenInputStream(input, 0)) {
            return False;
        }
    }
    else {
        return Fail;
    }

    /* read the file */
    READ_INNER();
    if (STATE(UserHasQuit)) {
        STATE(UserHasQuit) = 0;    // stop recovery here
        return Fail;
    }
    return True;
}


/****************************************************************************
**
*F  FuncREAD_STREAM( <self>, <stream> )   . . . . . . . . . . . read a stream
*/
static Obj FuncREAD_STREAM(Obj self, Obj stream)
{
    RequireInputStream(SELF_NAME, stream);

    /* try to open the file                                                */
    if (!OpenInputStream(stream, 0)) {
        return False;
    }

    /* read the test file                                                  */
    READ_INNER();
    return True;
}

/****************************************************************************
**
*F  FuncREAD_STREAM_LOOP( <self>, <instream>, <outstream> ) . . read a stream
**
**  Read data from <instream> in a read-eval-view loop and write all output
**  to <outstream>.
*/
static Obj FuncREAD_STREAM_LOOP_WITH_CONTEXT(Obj self,
                                             Obj instream,
                                             Obj outstream,
                                             Obj context)
{
    Int res;

    RequireInputStream(SELF_NAME, instream);
    RequireOutputStream(SELF_NAME, outstream);

    if (!OpenInputStream(instream, 0)) {
        return False;
    }

    if (!OpenOutputStream(outstream)) {
        res = CloseInput();
        GAP_ASSERT(res);
        return False;
    }

    LockCurrentOutput(1);
    READ_TEST_OR_LOOP(context);
    LockCurrentOutput(0);

    res = CloseInput();
    GAP_ASSERT(res);

    res &= CloseOutput();
    GAP_ASSERT(res);

    return res ? True : False;
}

static Obj FuncREAD_STREAM_LOOP(Obj self, Obj stream, Obj catcherrstdout)
{
    return FuncREAD_STREAM_LOOP_WITH_CONTEXT(self, stream, catcherrstdout, 0);
}
/****************************************************************************
**
*F  FuncREAD_AS_FUNC( <self>, <filename> )  . . . . . . . . . . . read a file
*/
static Obj FuncREAD_AS_FUNC(Obj self, Obj filename)
{
    RequireStringRep(SELF_NAME, filename);

    /* try to open the file                                                */
    if ( ! OpenInput( CONST_CSTR_STRING(filename) ) ) {
        return Fail;
    }

    /* read the function                                                   */
    return READ_AS_FUNC();
}


/****************************************************************************
**
*F  FuncREAD_AS_FUNC_STREAM( <self>, <stream> ) . . . . . . . . read a file
*/
static Obj FuncREAD_AS_FUNC_STREAM(Obj self, Obj stream)
{
    RequireInputStream(SELF_NAME, stream);

    /* try to open the file                                                */
    if (!OpenInputStream(stream, 0)) {
        return Fail;
    }

    /* read the function                                                   */
    return READ_AS_FUNC();
}


/****************************************************************************
**
*F  FuncREAD_GAP_ROOT( <self>, <filename> ) . . . . . . . . . . . read a file
*/
static Obj FuncREAD_GAP_ROOT(Obj self, Obj filename)
{
    Char filenamecpy[GAP_PATH_MAX];

    RequireStringRep(SELF_NAME, filename);

    /* Copy to avoid garbage collection moving string                      */
    strlcpy(filenamecpy, CONST_CSTR_STRING(filename), GAP_PATH_MAX);
    /* try to open the file                                                */
    return READ_GAP_ROOT(filenamecpy) ? True : False;
}


/****************************************************************************
**
*F  FuncTmpName( <self> ) . . . . . . . . . . . . . . return a temporary name
*/
static Obj FuncTmpName(Obj self)
{
#ifdef SYS_IS_CYGWIN32
    char name[] = "C:/WINDOWS/Temp/gaptempfile.XXXXXX";
#else
    char name[] = "/tmp/gaptempfile.XXXXXX";
#endif
    close(mkstemp(name));
    return MakeString(name);
}


/****************************************************************************
**
*F  FuncTmpDirectory( <self> )  . . . . . . . .  return a temporary directory
*/
static Obj FuncTmpDirectory(Obj self)
{
    char   name[GAP_PATH_MAX];
    char * env_tmpdir = getenv("TMPDIR");
    if (env_tmpdir != NULL) {
        strxcpy(name, env_tmpdir, sizeof(name));
        strxcat(name, "/", sizeof(name));
    }
    else {
#ifdef SYS_IS_CYGWIN32
        strxcpy(name, "C:/WINDOWS/Temp/", sizeof(name));
#else
        strxcpy(name, "/tmp/", sizeof(name));
#endif
    }
    strxcat(name, "gaptempdirXXXXXX", sizeof(name));

    char * tmp = mkdtemp(name);
    if (tmp == 0)
        return Fail;
    return MakeString(tmp);
}


/****************************************************************************
**
*F  FuncRemoveFile( <self>, <name> )  . . . . . . . . . .  remove file <name>
*/
static Obj FuncRemoveFile(Obj self, Obj filename)
{
    RequireStringRep(SELF_NAME, filename);

    /* call the system dependent function                                  */
    return SyRemoveFile( CONST_CSTR_STRING(filename) ) == -1 ? Fail : True;
}

/****************************************************************************
**
*F  FuncCreateDir( <self>, <name> )  . . . . . . . . . . . . create directory
*/
static Obj FuncCreateDir(Obj self, Obj filename)
{
    RequireStringRep(SELF_NAME, filename);

    /* call the system dependent function                                  */
    return SyMkdir( CONST_CSTR_STRING(filename) ) == -1 ? Fail : True;
}

/****************************************************************************
**
*F  FuncRemoveDir( <self>, <name> )  . . . . . . . . . . . . remove directory
*/
static Obj FuncRemoveDir(Obj self, Obj filename)
{
    RequireStringRep(SELF_NAME, filename);

    /* call the system dependent function                                  */
    return SyRmdir( CONST_CSTR_STRING(filename) ) == -1 ? Fail : True;
}

/****************************************************************************
**
*F  FuncIsDir( <self>, <name> )  . . . . . check whether something is a dir
*/
static Obj FuncIsDir(Obj self, Obj filename)
{
    RequireStringRep(SELF_NAME, filename);

    /* call the system dependent function                                  */
    return SyIsDir( CONST_CSTR_STRING(filename) );
}




/****************************************************************************
**
*F * * * * * * * * * * * file access test functions * * * * * * * * * * * * *
*/


/****************************************************************************
**
*F  FuncLastSystemError( <self> ) .  . . . . . .  return the last system error
*/
static UInt ErrorMessageRNam;
static UInt ErrorNumberRNam;

static Obj FuncLastSystemError(Obj self)
{
    Obj             err;
    Obj             msg;

    /* constructed an error record                                         */
    err = NEW_PREC(0);

    /* check if an errors has occured                                      */
    if ( SyLastErrorNo != 0 ) {
        ASS_REC( err, ErrorNumberRNam, INTOBJ_INT(SyLastErrorNo) );
        msg = MakeString(SyLastErrorMessage);
        ASS_REC( err, ErrorMessageRNam, msg );
    }

    /* no error has occured                                                */
    else {
        ASS_REC( err, ErrorNumberRNam, INTOBJ_INT(0) );
        msg = MakeString("no error");
        ASS_REC( err, ErrorMessageRNam, msg );
    }

    /* return the error record                                             */
    return err;
}


/****************************************************************************
**
*F  FuncIsExistingFile( <self>, <name> )  . . . . . . does file <name> exists
*/
static Obj FuncIsExistingFile(Obj self, Obj filename)
{
    Int             res;

    RequireStringRep(SELF_NAME, filename);

    /* call the system dependent function                                  */
    res = SyIsExistingFile( CONST_CSTR_STRING(filename) );
    return res == -1 ? False : True;
}


/****************************************************************************
**
*F  FuncIsReadableFile( <self>, <name> )  . . . . . . is file <name> readable
*/
static Obj FuncIsReadableFile(Obj self, Obj filename)
{
    Int             res;

    RequireStringRep(SELF_NAME, filename);

    /* call the system dependent function                                  */
    res = SyIsReadableFile( CONST_CSTR_STRING(filename) );
    return res == -1 ? False : True;
}


/****************************************************************************
**
*F  FuncIsWritableFile( <self>, <name> )  . . . . . . is file <name> writable
*/
static Obj FuncIsWritableFile(Obj self, Obj filename)
{
    Int             res;

    RequireStringRep(SELF_NAME, filename);

    /* call the system dependent function                                  */
    res = SyIsWritableFile( CONST_CSTR_STRING(filename) );
    return res == -1 ? False : True;
}


/****************************************************************************
**
*F  FuncIsExecutableFile( <self>, <name> )  . . . . is file <name> executable
*/
static Obj FuncIsExecutableFile(Obj self, Obj filename)
{
    Int             res;

    RequireStringRep(SELF_NAME, filename);

    /* call the system dependent function                                  */
    res = SyIsExecutableFile( CONST_CSTR_STRING(filename) );
    return res == -1 ? False : True;
}


/****************************************************************************
**
*F  FuncIsDirectoryPath( <self>, <name> ) . . . .  is file <name> a directory
*/
static Obj FuncIsDirectoryPathString(Obj self, Obj filename)
{
    Int             res;

    RequireStringRep(SELF_NAME, filename);

    /* call the system dependent function                                  */
    res = SyIsDirectoryPath( CONST_CSTR_STRING(filename) );
    return res == -1 ? False : True;
}


/****************************************************************************
**
*F  FuncLIST_DIR( <self>, <dirname> ) . . . read names of files in dir
**
**  This function returns a GAP list which contains the names of all files
**  contained in a directory <dirname>.
**
**  If <dirname> could not be opened as a directory 'fail' is returned. The
**  reason for the error can be found with 'LastSystemError();' in GAP.
**
*/
static Obj FuncLIST_DIR(Obj self, Obj dirname)
{
    DIR *dir;
    struct dirent *entry;
    Obj res;

    RequireStringRep(SELF_NAME, dirname);

    SyClearErrorNo();
    dir = opendir(CONST_CSTR_STRING(dirname));
    if (dir == NULL) {
        SySetErrorNo();
        return Fail;
    }
    res = NEW_PLIST(T_PLIST, 16);
    while ((entry = readdir(dir))) {
        PushPlist(res, MakeImmString(entry->d_name));
    }
    closedir(dir);
    return res;
}

/****************************************************************************
**
*F * * * * * * * * * * * * text stream functions  * * * * * * * * * * * * * *
*/

/****************************************************************************
**
*F  FuncCLOSE_FILE( <self>, <fid> ) . . . . . . . . . . . . .  close a stream
*/
static Obj FuncCLOSE_FILE(Obj self, Obj fid)
{
    Int ifid = GetSmallInt("CLOSE_FILE", fid);
    
    /* call the system dependent function                                  */
    Int ret = SyFclose( ifid );
    return ret == -1 ? Fail : True;
}


/****************************************************************************
**
*F  FuncINPUT_TEXT_FILE( <self>, <name> ) . . . . . . . . . . . open a stream
*/
static Obj FuncINPUT_TEXT_FILE(Obj self, Obj filename)
{
    Int             fid;

    RequireStringRep(SELF_NAME, filename);

    /* call the system dependent function                                  */
    SyClearErrorNo();
    fid = SyFopen( CONST_CSTR_STRING(filename), "r" );
    if ( fid == - 1)
        SySetErrorNo();
    return fid == -1 ? Fail : INTOBJ_INT(fid);
}


/****************************************************************************
**
*F  FuncIS_END_OF_FILE( <self>, <fid> ) . . . . . . . . . . .  is end of file
*/
static Obj FuncIS_END_OF_FILE(Obj self, Obj fid)
{
    Int ifid = GetSmallInt("IS_END_OF_FILE", fid);
    
    Int ret = SyIsEndOfFile( ifid );
    return ret == -1 ? Fail : ( ret == 0 ? False : True );
}


/****************************************************************************
**
*F  FuncOUTPUT_TEXT_FILE( <self>, <name>, <append> )  . . . . . open a stream
*/
static Obj FuncOUTPUT_TEXT_FILE(Obj self, Obj filename, Obj append)
{
    Int             fid;

    RequireStringRep(SELF_NAME, filename);
    RequireTrueOrFalse(SELF_NAME, append);

    /* call the system dependent function                                  */
    SyClearErrorNo();
    if ( append == True ) {
        fid = SyFopen( CONST_CSTR_STRING(filename), "a" );
    }
    else {
        fid = SyFopen( CONST_CSTR_STRING(filename), "w" );
    }
    if ( fid == - 1)
        SySetErrorNo();
    return fid == -1 ? Fail : INTOBJ_INT(fid);
}


/****************************************************************************
**
*F  FuncPOSITION_FILE( <self>, <fid> )  . . . . . . . . .  position of stream
*/
static Obj FuncPOSITION_FILE(Obj self, Obj fid)
{
    Int ifid = GetSmallInt("POSITION_FILE", fid);

    Int ret = SyFtell(ifid);

    // Return if failed
    if (ret == -1) {
        return Fail;
    }

    return INTOBJ_INT(ret);
}



/****************************************************************************
**
*F  FuncREAD_BYTE_FILE( <self>, <fid> ) . . . . . . . . . . . . . read a byte
*/
static Obj FuncREAD_BYTE_FILE(Obj self, Obj fid)
{
    Int ifid = GetSmallInt("READ_BYTE_FILE", fid);
    
    /* call the system dependent function                                  */
    Int ret = SyGetch( ifid );

    return ret == EOF ? Fail : INTOBJ_INT(ret);
}


/****************************************************************************
**
*F  FuncREAD_LINE_FILE( <self>, <fid> ) . . . . . . . . . . . . . read a line
**  
**  This uses fgets and works only if there are no zero characters in <fid>.
*/
static Obj FuncREAD_LINE_FILE(Obj self, Obj fid)
{
    Char            buf[256];
    Char *          cstr;
    Int             len, buflen;
    UInt            lstr;
    Obj             str;

    Int ifid = GetSmallInt("READ_LINE_FILE", fid);

    /* read <fid> until we see a newline or eof or we've read at least
       one byte and more are not immediately available */
    str = NEW_STRING(0);
    len = 0;
    while (1) {
      if ( len > 0 && !HasAvailableBytes(ifid))
        break;
      len += 255;
      GROW_STRING( str, len );
      if ( SyFgetsSemiBlock( buf, 256, ifid ) == 0 )
        break;
      buflen = strlen(buf);
      lstr = GET_LEN_STRING(str);
      cstr = CSTR_STRING(str) + lstr;
      memcpy( cstr, buf, buflen+1 );
      SET_LEN_STRING(str, lstr+buflen);
      if ( buf[buflen-1] == '\n' )
        break;
    }

    /* fix the length of <str>                                             */
    len = GET_LEN_STRING(str);
    ResizeBag( str, SIZEBAG_STRINGLEN(len) );

    /* and return                                                          */
    return len == 0 ? Fail : str;
}

/****************************************************************************
**
*F  FuncREAD_ALL_FILE( <self>, <fid>, <limit> )  . . . . . . . read remainder
**  
** more precisely, read until either
**   (a) we have read at least one byte and no more are available
**   (b) we have evidence that it will never be possible to read a byte
**   (c) we have read <limit> bytes (-1 indicates no limit)
*/

static Obj FuncREAD_ALL_FILE(Obj self, Obj fid, Obj limit)
{
    Char            buf[20000];
    Int             len;
    // Length of string read this loop (or negative for error)
    UInt            lstr;
    Obj             str;
    UInt            csize;

    Int ifid = GetSmallInt("READ_ALL_FILE", fid);
    Int ilim = GetSmallInt("READ_ALL_FILE", limit);

    /* read <fid> until we see  eof or we've read at least
       one byte and more are not immediately available */
    str = NEW_STRING(0);
    len = 0;
    lstr = 0;

#ifdef SYS_IS_CYGWIN32
 getmore:
#endif
    while (ilim == -1 || len < ilim ) {
      lstr = 0;
      if ( len > 0 && !HasAvailableBytes(ifid))
          break;
      if (SyBufIsTTY(ifid)) {
          if (ilim == -1) {
              Pr("#W Warning -- reading to  end of input tty will never "
                 "end\n",
                 0, 0);
              csize = 20000;
          }
          else
              csize = ((ilim - len) > 20000) ? 20000 : ilim - len;

          if (SyFgetsSemiBlock(buf, csize, ifid))
              lstr = strlen(buf);
          else
              lstr = 0;
      }
      else {
          do {
              csize =
                  (ilim == -1 || (ilim - len) > 20000) ? 20000 : ilim - len;
              lstr = SyReadWithBuffer(ifid, buf, csize);
          } while (lstr == -1 && errno == EAGAIN);
      }
      if (lstr <= 0) {
          SyBufSetEOF(ifid);
          break;
      }
      GROW_STRING( str, len+lstr );
      memcpy(CHARS_STRING(str)+len, buf, lstr);
      len += lstr;
      SET_LEN_STRING(str, len);
    }

    /* fix the length of <str>                                             */
    len = GET_LEN_STRING(str);
#ifdef SYS_IS_CYGWIN32
    /* line end hackery */
    UInt i = 0, j = 0;
    while (i < len) {
        if (CHARS_STRING(str)[i] == '\r') {
            if (i < len - 1 && CHARS_STRING(str)[i + 1] == '\n') {
                i++;
                continue;
            }
            else
                CHARS_STRING(str)[i] = '\n';
        }
        CHARS_STRING(str)[j++] = CHARS_STRING(str)[i++];
    }
    len = j;
    SET_LEN_STRING(str, len);
    // If we have not yet read enough, and have read at least one character this loop, then read more
    if (ilim != -1 && len < ilim && lstr > 0)
        goto getmore;
#endif
    ResizeBag( str, SIZEBAG_STRINGLEN(len) );

    /* and return                                                          */
    return len == 0 ? Fail : str;
}

/****************************************************************************
**
*F  FuncSEEK_POSITION_FILE( <self>, <fid>, <pos> )  . seek position of stream
*/
static Obj FuncSEEK_POSITION_FILE(Obj self, Obj fid, Obj pos)
{
    Int             ret;

    Int ifid = GetSmallInt("SEEK_POSITION_FILE", fid);
    Int ipos = GetSmallInt("SEEK_POSITION_FILE", pos);
    
    ret = SyFseek( ifid, ipos );
    return ret == -1 ? Fail : True;
}


/****************************************************************************
**
*F  FuncWRITE_BYTE_FILE( <self>, <fid>, <byte> )  . . . . . . .  write a byte
*/
static Obj FuncWRITE_BYTE_FILE(Obj self, Obj fid, Obj ch)
{
    Int ifid = GetSmallInt("WRITE_BYTE_FILE", fid);
    Int ich = GetSmallInt("WRITE_BYTE_FILE", ch);
    
    /* call the system dependent function                                  */
    Int ret = SyEchoch( ich, ifid );
    return ret == -1 ? Fail : True;
}

/****************************************************************************
**
*F  FuncWRITE_STRING_FILE_NC( <self>, <fid>, <string> ) .write a whole string
*/
static Obj FuncWRITE_STRING_FILE_NC(Obj self, Obj fid, Obj str)
{
    Int             len = 0, l, ret;
    const char      *ptr;

    /* don't check the argument                                            */
    
    len = GET_LEN_STRING(str);
    ptr = CONST_CSTR_STRING(str);
    while (len > 0) {
      l = (len > 1048576) ? 1048576 : len;
      ret = SyWrite(INT_INTOBJ(fid), ptr, l);
      if (ret == -1) {
        SySetErrorNo();
        return Fail;
      }
      len -= ret;
      ptr += ret;
    }
    return True;
}

static Obj FuncREAD_STRING_FILE(Obj self, Obj fid)
{
    Int ifid = GetSmallInt("READ_STRING_FILE", fid);
    return SyReadStringFid(ifid);
}

/****************************************************************************
**
*F  FuncFD_OF_FILE( <fid> )
*/
static Obj FuncFD_OF_FILE(Obj self, Obj fid)
{
    Int fd = GetSmallInt("FD_OF_FILE", fid);
    Int fdi = SyBufFileno(fd);
    return INTOBJ_INT(fdi);
}

#ifdef HPCGAP
static Obj FuncRAW_MODE_FILE(Obj self, Obj fid, Obj onoff)
{
    Int fd = GetSmallInt("RAW_MODE_FILE", fid);
    if (onoff == False || onoff == Fail) {
        syStopraw(fd);
        return False;
    }
    else
        return syStartraw(fd) ? True : False;
}
#endif

#ifdef HAVE_SELECT
static Obj FuncUNIXSelect(Obj self,
                          Obj inlist,
                          Obj outlist,
                          Obj exclist,
                          Obj timeoutsec,
                          Obj timeoutusec)
{
  fd_set infds,outfds,excfds;
  struct timeval tv;
  int n,maxfd;
  Int i,j;
  Obj o;

  RequirePlainList(SELF_NAME, inlist);
  RequirePlainList(SELF_NAME, outlist);
  RequirePlainList(SELF_NAME, exclist);

  FD_ZERO(&infds);
  FD_ZERO(&outfds);
  FD_ZERO(&excfds);
  maxfd = 0;
  /* Handle input file descriptors: */
  for (i = 1;i <= LEN_PLIST(inlist);i++) {
    o = ELM_PLIST(inlist,i);
    if (o != (Obj) 0 && IS_INTOBJ(o)) {
      j = INT_INTOBJ(o);  /* a UNIX file descriptor */
      FD_SET(j,&infds);
      if (j > maxfd) maxfd = j;
    }
  }
  /* Handle output file descriptors: */
  for (i = 1;i <= LEN_PLIST(outlist);i++) {
    o = ELM_PLIST(outlist,i);
    if (o != (Obj) 0 && IS_INTOBJ(o)) {
      j = INT_INTOBJ(o);  /* a UNIX file descriptor */
      FD_SET(j,&outfds);
      if (j > maxfd) maxfd = j;
    }
  }
  /* Handle exception file descriptors: */
  for (i = 1;i <= LEN_PLIST(exclist);i++) {
    o = ELM_PLIST(exclist,i);
    if (o != (Obj) 0 && IS_INTOBJ(o)) {
      j = INT_INTOBJ(o);  /* a UNIX file descriptor */
      FD_SET(j,&excfds);
      if (j > maxfd) maxfd = j;
    }
  }
  /* Handle the timeout: */
  if (timeoutsec != (Obj) 0 && IS_INTOBJ(timeoutsec) &&
      timeoutusec != (Obj) 0 && IS_INTOBJ(timeoutusec)) {
    tv.tv_sec = INT_INTOBJ(timeoutsec);
    tv.tv_usec = INT_INTOBJ(timeoutusec);
    n = select(maxfd+1,&infds,&outfds,&excfds,&tv);
  } else {
    n = select(maxfd+1,&infds,&outfds,&excfds,NULL);
  }
    
  if (n >= 0) {
    /* Now run through the lists and call functions if ready: */

    for (i = 1;i <= LEN_PLIST(inlist);i++) {
      o = ELM_PLIST(inlist,i);
      if (o != (Obj) 0 && IS_INTOBJ(o)) {
        j = INT_INTOBJ(o);  /* a UNIX file descriptor */
        if (!(FD_ISSET(j,&infds))) {
          SET_ELM_PLIST(inlist,i,Fail);
          CHANGED_BAG(inlist);
        }
      }
    }
    /* Handle output file descriptors: */
    for (i = 1;i <= LEN_PLIST(outlist);i++) {
      o = ELM_PLIST(outlist,i);
      if (o != (Obj) 0 && IS_INTOBJ(o)) {
        j = INT_INTOBJ(o);  /* a UNIX file descriptor */
        if (!(FD_ISSET(j,&outfds))) {
          SET_ELM_PLIST(outlist,i,Fail);
          CHANGED_BAG(outlist);
        }
      }
    }
    /* Handle exception file descriptors: */
    for (i = 1;i <= LEN_PLIST(exclist);i++) {
      o = ELM_PLIST(exclist,i);
      if (o != (Obj) 0 && IS_INTOBJ(o)) {
        j = INT_INTOBJ(o);  /* a UNIX file descriptor */
        if (!(FD_ISSET(j,&excfds))) {
          SET_ELM_PLIST(exclist,i,Fail);
          CHANGED_BAG(exclist);
        }
      }
    }
  }
  return INTOBJ_INT(n);
}
#endif

/****************************************************************************
**
*F * * * * * * * * * * * * * execution functions  * * * * * * * * * * * * * *
*/


/****************************************************************************
**
*F  FuncExecuteProcess( <self>, <dir>, <prg>, <in>, <out>, <args> )   process
*/
static Obj
FuncExecuteProcess(Obj self, Obj dir, Obj prg, Obj in, Obj out, Obj args)
{
    Obj    ExecArgs[1024];
    Char * ExecCArgs[1024];

    Obj                 tmp;
    Int                 res;
    Int                 i;

    RequireStringRep(SELF_NAME, dir);
    RequireStringRep(SELF_NAME, prg);
    Int iin = GetSmallInt("ExecuteProcess", in);
    Int iout = GetSmallInt("ExecuteProcess", out);
    RequireSmallList(SELF_NAME, args);

    /* create an argument array                                            */
    for ( i = 1;  i <= LEN_LIST(args);  i++ ) {
        if ( i == 1023 )
            break;
        tmp = ELM_LIST( args, i );
        RequireStringRep(SELF_NAME, tmp);
        ExecArgs[i] = tmp;
    }
    ExecCArgs[0]   = CSTR_STRING(prg);
    ExecCArgs[i] = 0;
    for ( i--;  0 < i;  i-- ) {
        ExecCArgs[i] = CSTR_STRING(ExecArgs[i]);
    }
    if (SyWindow && out == INTOBJ_INT(1)) /* standard output */
      syWinPut( INT_INTOBJ(out), "@z","");

    /* execute the process                                                 */
    res = SyExecuteProcess( CSTR_STRING(dir),
                            CSTR_STRING(prg),
                            iin,
                            iout,
                            ExecCArgs );

    if (SyWindow && out == INTOBJ_INT(1)) /* standard output */
      syWinPut( INT_INTOBJ(out), "@mAgIc","");
    return res == 255 ? Fail : INTOBJ_INT(res);
}


/****************************************************************************
**
*F * * * * * * * * * * * * * initialize module * * * * * * * * * * * * * * *
*/

/****************************************************************************
**
*V  GVarFuncs . . . . . . . . . . . . . . . . . . list of functions to export
*/
static StructGVarFunc GVarFuncs[] = {

    GVAR_FUNC_1ARGS(READ, filename),
    GVAR_FUNC_1ARGS(READ_NORECOVERY, filename),
    GVAR_FUNC_4ARGS(READ_ALL_COMMANDS, instream, echo, capture, outputFunc),
    GVAR_FUNC_2ARGS(READ_COMMAND_REAL, stream, echo),
    GVAR_FUNC_1ARGS(READ_STREAM, stream),
    GVAR_FUNC_2ARGS(READ_STREAM_LOOP, stream, catchstderrout),
    GVAR_FUNC_3ARGS(
        READ_STREAM_LOOP_WITH_CONTEXT, stream, catchstderrout, context),
    GVAR_FUNC_1ARGS(READ_AS_FUNC, filename),
    GVAR_FUNC_1ARGS(READ_AS_FUNC_STREAM, stream),
    GVAR_FUNC_1ARGS(READ_GAP_ROOT, filename),
    GVAR_FUNC_1ARGS(LOG_TO, filename),
    GVAR_FUNC_1ARGS(LOG_TO_STREAM, filename),
    GVAR_FUNC_0ARGS(CLOSE_LOG_TO),
    GVAR_FUNC_1ARGS(INPUT_LOG_TO, filename),
    GVAR_FUNC_1ARGS(INPUT_LOG_TO_STREAM, filename),
    GVAR_FUNC_0ARGS(CLOSE_INPUT_LOG_TO),
    GVAR_FUNC_1ARGS(OUTPUT_LOG_TO, filename),
    GVAR_FUNC_1ARGS(OUTPUT_LOG_TO_STREAM, filename),
    GVAR_FUNC_0ARGS(CLOSE_OUTPUT_LOG_TO),
    GVAR_FUNC(Print, -1, "args"),
    GVAR_FUNC(PRINT_TO, -1, "args"),
    GVAR_FUNC(PRINT_TO_STREAM, -1, "args"),
    GVAR_FUNC(APPEND_TO, -1, "args"),
    GVAR_FUNC(APPEND_TO_STREAM, -1, "args"),
    GVAR_FUNC_0ARGS(TmpName),
    GVAR_FUNC_0ARGS(TmpDirectory),
    GVAR_FUNC_1ARGS(RemoveFile, filename),
    GVAR_FUNC_1ARGS(CreateDir, filename),
    GVAR_FUNC_1ARGS(RemoveDir, filename),
    GVAR_FUNC_1ARGS(IsDir, filename),
    GVAR_FUNC_0ARGS(LastSystemError),
    GVAR_FUNC_1ARGS(IsExistingFile, filename),
    GVAR_FUNC_1ARGS(IsReadableFile, filename),
    GVAR_FUNC_1ARGS(IsWritableFile, filename),
    GVAR_FUNC_1ARGS(IsExecutableFile, filename),
    GVAR_FUNC_1ARGS(IsDirectoryPathString, filename),
    GVAR_FUNC_1ARGS(LIST_DIR, dirname),
    GVAR_FUNC_1ARGS(CLOSE_FILE, fid),
    GVAR_FUNC_1ARGS(INPUT_TEXT_FILE, filename),
    GVAR_FUNC_2ARGS(OUTPUT_TEXT_FILE, filename, append),
    GVAR_FUNC_1ARGS(IS_END_OF_FILE, fid),
    GVAR_FUNC_1ARGS(POSITION_FILE, fid),
    GVAR_FUNC_1ARGS(READ_BYTE_FILE, fid),
    GVAR_FUNC_1ARGS(READ_LINE_FILE, fid),
    GVAR_FUNC_2ARGS(READ_ALL_FILE, fid, limit),
    GVAR_FUNC_2ARGS(SEEK_POSITION_FILE, fid, pos),
    GVAR_FUNC_2ARGS(WRITE_BYTE_FILE, fid, byte),
    GVAR_FUNC_2ARGS(WRITE_STRING_FILE_NC, fid, string),
    GVAR_FUNC_1ARGS(READ_STRING_FILE, fid),
    GVAR_FUNC_1ARGS(FD_OF_FILE, fid),
#ifdef HPCGAP
    GVAR_FUNC_2ARGS(RAW_MODE_FILE, fid, onoff),
#endif
#ifdef HAVE_SELECT
    GVAR_FUNC_5ARGS(
        UNIXSelect, inlist, outlist, exclist, timeoutsec, timeoutusec),
#endif
    GVAR_FUNC_5ARGS(ExecuteProcess, dir, prg, in, out, args),
    { 0, 0, 0, 0, 0 }

};


/****************************************************************************
**
*F  InitKernel( <module> )  . . . . . . . . initialise kernel data structures
*/
static Int InitKernel (
    StructInitInfo *    module )
{
    /* init filters and functions                                          */
    InitHdlrFuncsFromTable( GVarFuncs );

    ImportFuncFromLibrary( "IsInputStream", &IsInputStream );
    ImportFuncFromLibrary( "IsOutputStream", &IsOutputStream );

    return 0;
}


/****************************************************************************
**
*F  PostRestore( <module> ) . . . . . . . . . . . . . after restore workspace
*/
static Int PostRestore (
    StructInitInfo *    module )
{
    /* file access test functions                                          */
    ErrorNumberRNam  = RNamName("number");
    ErrorMessageRNam = RNamName("message");

    /* pick up the number of this global */
    LastReadValueGVar = GVarName("LastReadValue");

    return 0;
}


/****************************************************************************
**
*F  InitLibrary( <module> ) . . . . . . .  initialise library data structures
*/
static Int InitLibrary (
    StructInitInfo *    module )
{
    /* init filters and functions                                          */
    InitGVarFuncsFromTable( GVarFuncs );


    return PostRestore( module );
}


/****************************************************************************
**
*F  InitInfoStreams() . . . . . . . . . . . . . . . . table of init functions
*/
static StructInitInfo module = {
    // init struct using C99 designated initializers; for a full list of
    // fields, please refer to the definition of StructInitInfo
    .type = MODULE_BUILTIN,
    .name = "streams",
    .initKernel = InitKernel,
    .initLibrary = InitLibrary,
    .postRestore = PostRestore,
};

StructInitInfo * InitInfoStreams ( void )
{
    return &module;
}
