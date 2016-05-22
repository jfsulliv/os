MODULES += $(dir)

sp              := $(sp).x
dirstack_$(sp)  := $(d)
d               := $(dir)

INC             +=$(d)/include

dir := $(d)/mm
include $(dir)/module.mk
dir := $(d)/machine
include $(dir)/module.mk

d               := $(dirstack_$(sp))
sp              := $(basename $(sp))
