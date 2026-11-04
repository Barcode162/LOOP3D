#pragma once
// The machine's instructions (one byte, then 16-bit operands).
#include <cstdint>

namespace loop::script {

enum Op : uint8_t {
  OP_CONST,        // k            push constants[k]
  OP_NOTHING,
  OP_TRUE,
  OP_FALSE,
  OP_POP,
  OP_DUP,
  OP_GET_LOCAL,    // slot
  OP_SET_LOCAL,    // slot         (pops)
  OP_GET_CELL,     // slot         local holding a cell
  OP_SET_CELL,     // slot
  OP_BOX,          //              wrap top in a new cell
  OP_GET_UPVAL,    // i
  OP_SET_UPVAL,    // i
  OP_GET_GLOBAL,   // k name
  OP_SET_GLOBAL,   // k name
  OP_GET_FIELD,    // k name
  OP_SET_FIELD,    // k name       (obj value → )
  OP_GET_INDEX,
  OP_SET_INDEX,    //              (obj idx value → )
  OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD, OP_POW, OP_NEG, OP_NOT,
  OP_EQ, OP_NE, OP_LT, OP_LE, OP_GT, OP_GE, OP_IN,
  OP_JUMP,         // offset (forward, unsigned)
  OP_LOOP,         // offset (backward)
  OP_JUMP_FALSE,   // offset       pops
  OP_AND,          // offset       keeps the value when it decides
  OP_OR,           // offset
  OP_CALL,         // argc, named, k(names list)
  OP_RETURN,
  OP_CLOSURE,      // k proto
  OP_LIST,         // n
  OP_TABLE,        // n, k(keys list)
  OP_INTERP,       // n            join n values as text
  OP_ITER_PREP,    //              iterable → list snapshot
  OP_ITER_NEXT,    // listSlot, indexSlot, varSlot, offset(exit)
  OP_RANGE_NEXT,   // varSlot, endSlot, stepSlot, offset(exit)
  OP_RANGE_STEP,   //              (from to → from to step)  default step
  OP_DEFAULT,      // slot, offset skip if slot is set
  OP_REQUIRE,      // slot, k name
  OP_CELLIFY,      // slot         wrap a parameter in a cell
  OP_WAIT,
  OP_SCHEDULE,     // kind (0 every, 1 after, 2 when, 3 task)  (arg closure → )
  OP_EMIT,         //              (name payload → )
  OP_COUNT_CHECK,  // slot, offset   repeat-N loops: slot counts down
};

}  // namespace loop::script
