NAME      := procmgr_py.so

UTILS     := ../utils/
INCLCUDES := -I${UTILS} -I${UTILS}/ap -I${UTILS}/co -I${UTILS}/generic -I${UTILS}/pymod -I.
INCLCUDES += -I/usr/include/python3.8
INCLCUDES += -I../
LIBS      := -lpthread -ldl
LIBS      += -lpython3.8

SRCS      := $(wildcard ./*.cpp)
SRCS      += $(wildcard ${UTILS}/*.cpp)
SRCS      += $(wildcard ${UTILS}/pymod/*.cpp)
OBJS      := $(SRCS:.cpp=.o)
DEPS      := $(SRCS:.cpp=.d)
CXX 	  := g++-11
CXX_FLAGS := -std=c++2a -g -fPIC -O3 -export-dynamic
CXX_FLAGS += -Wno-format-security
CXX_FLAGS += -DPYMOD_MODULE_NAME=PyInit_procmgr_py

all: ${NAME}

${NAME}: ${DEPS} ${OBJS}
	${CXX} ${CXX_FLAGS} ${INCLCUDES} -shared ${OBJS} ${LIBS} -o $@

${DEPS}: makefile
${OBJS}: makefile

${DEPS}:%.d:%.cpp
	${CXX} -c ${CXX_FLAGS} ${INCLCUDES} -MM $< -MF $@

include ${DEPS}

${OBJS}:%.o:%.cpp
	${CXX} -c ${CXX_FLAGS} ${INCLCUDES} $< -o $@

clean:
	rm -f ${OBJS}
	rm -f ${DEPS}
	rm -f ${NAME}