NAME      := a.out
UTILS     := ./utils/

INCLCUDES := -I${UTILS} -I${UTILS}/ap -I${UTILS}/co -I${UTILS}/generic -I.
LIBS      := -lpthread -ldl

SRCS      := $(wildcard ./*.cpp)
SRCS      += $(wildcard ${UTILS}/*.cpp)
OBJS      := $(SRCS:.cpp=.o)
DEPS      := $(SRCS:.cpp=.d)
CXX 	  := g++-11
CXX_FLAGS := -std=c++2a -g -export-dynamic -O3
CXX_FLAGS += -Wno-format-security

all: ${NAME} daemons-build

daemons-build:
	make -C daemons/chanmgr
	make -C daemons/scheduler
	make -C daemons/taskmon
	make -C python-mod

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
	make -C daemons/chanmgr clean
	make -C daemons/scheduler clean
	make -C daemons/taskmon clean
	make -C python-mod clean
