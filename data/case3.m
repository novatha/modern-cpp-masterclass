function mpc = case3
% MATPOWER Case Format : Version 2
mpc.version = '2';
mpc.baseMVA = 100;

%% bus data
%	bus_i	type	Pd	Qd	Gs	Bs	area	Vm	Va	baseKV	zone	Vmax	Vmin
mpc.bus = [
	1	3	0	0	0	0	1	1	0	138	1	1.05	0.95;
	2	1	150	0	0	0	1	1	0	138	1	1.05	0.95;
	3	1	200	0	0	0	1	1	0	138	1	1.05	0.95;
];

%% branch data
%	fbus	tbus	r	x	b	rateA	rateB	rateC	ratio	angle	status	angmin	angmax
mpc.branch = [
	1	2	0.01	0.10	0	0	0	0	0	0	1	-360	360;
	1	3	0.01	0.15	0	0	0	0	0	0	1	-360	360;
	2	3	0.02	0.20	0	0	0	0	0	0	1	-360	360;
];
