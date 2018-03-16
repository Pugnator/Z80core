/**
 *
 * @file   cpu.cc
 * @date   16.03.2018 
 * @license This project is released under the GPL 2 license.
 * @brief 
 *
 */

#include <cpu.hpp>
#include <stdio.h>

void vcpu::reset()
{
	std::fill(cached_ram->begin(), cached_ram->end(), 0);
	state = FETCH;
	flag = NONE;
	pins = {};
	regs = {};
	regs.PC = 0xFFFF;
	regs.SP = 0xFFFF;
	clocks_passed = 0;
}

void vcpu::clock()
{

}

void vcpu::fetch()
{

}
