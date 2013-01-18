/*
This file is part of Bohrium and copyright (c) 2012 the Bohrium
team <http://www.bh107.org>.

Bohrium is free software: you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as 
published by the Free Software Foundation, either version 3 
of the License, or (at your option) any later version.

Bohrium is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the 
GNU Lesser General Public License along with Bohrium. 

If not, see <http://www.gnu.org/licenses/>.
*/

#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>
#include <bh.h>

#include "bh_vem_cluster.h"
#include "exec.h"
#include "dispatch.h"
#include "pgrid.h"


bh_error bh_vem_cluster_init(bh_component *self)
{
    bh_error e;

    //Initiate the process grid
    pgrid_init();

    if(pgrid_myrank != 0)
    {
        fprintf(stderr, "[VEM-CLUSTER] Multiple instants of the bridge is not allowed. "
        "Only rank-0 should execute the bridge. All slaves should execute bh_vem_cluster_slave.\n");
        MPI_Abort(MPI_COMM_WORLD, -1);
    }

    //Send the component name
    dispatch_reset();
    dispatch_add2payload(strlen(self->name)+1, self->name);
    dispatch_send(BH_CLUSTER_DISPATCH_INIT);

    //Execute our self
    if((e = exec_init(self->name)) != BH_SUCCESS)
        return e;

    return BH_SUCCESS; 
}


bh_error bh_vem_cluster_shutdown(void)
{
    //Send the component name
    dispatch_reset();
    dispatch_send(BH_CLUSTER_DISPATCH_SHUTDOWN);
    
    //Execute our self
    return exec_shutdown();
} 

    
bh_error bh_vem_cluster_reg_func(char *fun, bh_intp *id)
{
    bh_error e;
    assert(fun != NULL);

    //The Master will find the new 'id' if required.
    if((e = exec_reg_func(fun, id)) != BH_SUCCESS)
        return e;

    //Send the component name
    dispatch_reset();
    dispatch_add2payload(sizeof(bh_intp), id);
    dispatch_add2payload(strlen(fun)+1, fun);
    dispatch_send(BH_CLUSTER_DISPATCH_UFUNC);

    return BH_SUCCESS;
}


bh_error bh_vem_cluster_execute(bh_intp count,
                                      bh_instruction inst_list[])
{
    //Send the instruction list and operands to the slaves
    dispatch_inst_list(count, inst_list);
    
    return exec_execute(count,inst_list);
}
