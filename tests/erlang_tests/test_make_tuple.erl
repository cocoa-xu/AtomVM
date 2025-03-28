%
% This file is part of AtomVM.
%
% Copyright 2018 Davide Bettio <davide@uninstall.it>
%
% Licensed under the Apache License, Version 2.0 (the "License");
% you may not use this file except in compliance with the License.
% You may obtain a copy of the License at
%
%    http://www.apache.org/licenses/LICENSE-2.0
%
% Unless required by applicable law or agreed to in writing, software
% distributed under the License is distributed on an "AS IS" BASIS,
% WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
% See the License for the specific language governing permissions and
% limitations under the License.
%
% SPDX-License-Identifier: Apache-2.0 OR LGPL-2.1-or-later
%

-module(test_make_tuple).

-export([start/0, f/1, g/2]).

start() ->
    {} = ?MODULE:f(0),
    {hello, hello} = ?MODULE:f(2),
    % There are 35 terms available on heap.
    R = ?MODULE:g(34, ?MODULE:f(2)),
    {R} = ?MODULE:g(1, R),
    0.

f(N) ->
    erlang:make_tuple(N, hello).

g(N, Elem) ->
    erlang:make_tuple(N, Elem).
