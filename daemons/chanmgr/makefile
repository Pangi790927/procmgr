NAME      := chanmgr
UTILS     := ../../utils/

INCLCUDES := -I${UTILS} -I${UTILS}/ap -I${UTILS}/co -I${UTILS}/generic -I.
INCLCUDES += -I../../
LIBS      := -lpthread -ldl

SRCS      := $(wildcard ./*.cpp)
SRCS      += $(wildcard ${UTILS}/*.cpp)
OBJS      := $(SRCS:.cpp=.o)
DEPS      := $(SRCS:.cpp=.d)
CXX 	  := g++-11
CXX_FLAGS := -std=c++2a -g -export-dynamic -O3
CXX_FLAGS += -Wno-format-security

all: ${NAME}

${NAME}: ${DEPS} ${OBJS}
	${CXX} ${CXX_FLAGS} ${INCLCUDES} ${OBJS} ${LIBS} -o $@

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
