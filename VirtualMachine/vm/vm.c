#include "../../Bytecode/debug/debug.h"
#include "../../Bytecode/memory/memory.h"
#include "../../Bytecode/value/value.h"
#include "../../common/common.h"
#include "../../Compilation/compiler/compiler.h"
#include "../../Language/object/object.h"
#include "vm.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>


VM vm;

void initVM(){
    resetStack();
    vm.objects = NULL;
    //initTable(&vm.globals);
    initTable(&vm.globalNames);
    initTable(&vm.strings);
};

void freeVM(){
    //freeTable(&vm.globals);
    freeTable(&vm.globalNames);
    freeTable(&vm.strings);
    freeObjects();
};

static void resetStack(){
    vm.stackTop = vm.stack;
};

static void runtimeError(const char* format, ...){  //char* for error
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputs("\n", stderr);

    size_t instruction = vm.ip -vm.chunk->code - 1;
    uint16_t line = vm.chunk->lines[instruction].line;
    fprintf(stderr, "[line %d] in script\n", line);
    resetStack();
}

static bool isFalsey(Value value){
    return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

static void concatenate(){
    ObjString* b = AS_STRING(pop());
    ObjString* a = AS_STRING(pop());

    int16_t length = a->length + b->length;
    int8_t* chars = ALLOCATE(int8_t, length + 1);
    memcpy(chars, a->chars, a->length);
    memcpy(chars + a->length, b->chars, b->length);
    chars[length] = '\0';

    ObjString* result = takeString(chars, length);
    push(OBJ_VAL(result));
}

static InterpretResult run(){
#define READ_BYTE() (*vm.ip++)
#define READ_CONSTANT() (vm.chunk->constants.values[READ_BYTE()])
#define READ_STRING()   AS_STRING(READ_CONSTANT())
//For Doubles! Make versions for other number types.
#define BINARY_OP(valueType, op) \
    do{ \
        if(!IS_DOUBLE(check(0)) || !IS_DOUBLE(check(1))){ \
            runtimeError("Operands must be numbers."); \
            return INTERPRET_RUNTIME_ERROR; \
        } \
        double b = AS_DOUBLE(pop()); \
        double a = AS_DOUBLE(pop()); \
        push(valueType(a op b)); \
    } while(false)

#define BINARY_OPF(valueType, op) \
    do{ \
        if(!IS_DOUBLE(check(0)) || !IS_DOUBLE(check(1))){ \
            runtimeError("Operands must be numbers."); \
            return INTERPRET_RUNTIME_ERROR; \
        } \
        double b = AS_DOUBLE(pop()); \
        double a = AS_DOUBLE(pop()); \
        push(valueType(op(a,b))); \
    } while(false)


// #define INT_OP(op) \
//     do { \
//         int16_t b = pop(); \
//         int16_t a = pop(); \
//         push(a op b); \
//     } while(false)

    for(;;){
#ifdef DEBUG_TRACE_EXECUTION
    printf("          ");
    for(Value* slot = vm.stack; slot < vm.stackTop; slot++){
    // for(Value* slot = vm.stack; slot < vm.stack + vm.stackCount; slot++){
        printf("[ ");
        printValue(*slot);
        printf(" ]");
    }
    printf("\n");
    disassembleInstruction(vm.chunk, (int16_t)(vm.ip - vm.chunk->code));
#endif

        uint8_t instruction;
        switch(instruction = READ_BYTE()){
            case OP_CONSTANT:{
                Value constant = READ_CONSTANT();
                push(constant);
                break;
            }
            case OP_NIL:        push(NIL_VAL); break;
            case OP_TRUE:       push(BOOL_VAL(true)); break;
            case OP_FALSE:      push(BOOL_VAL(false)); break;
            case OP_POP: pop(); break;
            case OP_DEFINE_GLOBAL:{
                vm.globalValues.values[READ_BYTE()] = pop();
                break;
                // ObjString* name = READ_STRING();
                // tableSet(&vm.globals, name, check(0));
                // pop();
                // break;
            }
            case OP_SET_GLOBAL:{
                uint8_t index = READ_BYTE();
                if(IS_UNDEFINED(vm.globalValues.values[index])){
                    runtimeError("Undefined variable.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                vm.globalValues.values[index] = check(0);
                break;
                // ObjString* name = READ_STRING();
                // if(tableSet(&vm.globals, name, check(0))){
                //     tableDelete(&vm.globals, name);
                //     runtimeError("Undefined variable '%s'.", name->chars);
                //     return INTERPRET_RUNTIME_ERROR;
                // }
                // break;
            }
            case OP_GET_GLOBAL:{
                Value value = vm.globalValues.values[READ_BYTE()];
                if(IS_UNDEFINED(value)){
                    runtimeError("Undefined variable.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                push(value);
                break;
                // ObjString* name = READ_STRING();
                // Value value;
                // if(!tableGet(&vm.globals, name, &value)){
                //     runtimeError("Undefined variable %s.", name->chars);
                //     return INTERPRET_RUNTIME_ERROR;
                // }
                // push(value);
                // break;
            }
            case OP_EQUAL:{
                Value b = pop();
                Value a = pop(); 
                push(BOOL_VAL(valuesEqual(a,b)));
                break;
            }    
            case OP_LESS:       BINARY_OP(BOOL_VAL, <); break;
            case OP_GREATER:    BINARY_OP(BOOL_VAL, >); break;
            case OP_ADD:
                if(IS_STRING(check(0)) && IS_STRING(check(1))){
                    concatenate();
                } else if (IS_DOUBLE(check(0)) && IS_DOUBLE(check(1))){
                    double b = AS_DOUBLE(pop()); \
                    double a = AS_DOUBLE(pop()); \
                    push(DOUBLE_VAL(a + b)); \
                } else {
                    runtimeError("Operands must be doubles or strings.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;    
            case OP_SUBTRACT:   BINARY_OP(DOUBLE_VAL, -); break;
            case OP_MULTIPLY:   BINARY_OP(DOUBLE_VAL, *); break;
            case OP_DIVIDE:     BINARY_OP(DOUBLE_VAL, /); break;
            case OP_EXPONENTIATE: BINARY_OPF(DOUBLE_VAL, pow); break;
            case OP_MODULATE:   BINARY_OPF(DOUBLE_VAL, fmod); break;
            case OP_NOT:        push(BOOL_VAL(isFalsey(pop()))); break;
            case OP_NEGATE:{
                if(!IS_DOUBLE(check(0))){
                    runtimeError("Operand must be a number.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                push(DOUBLE_VAL(-AS_DOUBLE(pop()))); //will need to modify for different types.
                }
                break;
            case OP_PUTS:{
                printValue(pop());
                printf("\n");
                break;
            }
            case OP_RETURN:{
                return INTERPRET_OK;
            }
        }
    }

#undef READ_BYTE
#undef READ_CONSTANT
#undef READ_STRING
#undef BINARY_OP
#undef BINARY_OPF
}

InterpretResult interpret(const int8_t* source){
    Chunk chunk;
    initChunk(&chunk);

    if(!compile(source, &chunk)){
        freeChunk(&chunk);
        return INTERPRET_COMPILE_ERROR;
    }

    vm.chunk = &chunk;
    vm.ip = vm.chunk->code;

    InterpretResult result = run();

    freeChunk(&chunk);
    return result;
}

void push(Value value){
    *vm.stackTop = value;
    vm.stackTop++;
}

Value pop(){
    vm.stackTop--;
    return *vm.stackTop;

    // vm.stackCount--;
    // return vm.stack[vm.stackCount];
}

static Value check(int16_t distance){
    return vm.stackTop[-1 - distance];
}


