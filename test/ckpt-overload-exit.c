/*
 * Copyright (c) 2008 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Steven Dake (sdake@redhat.com)
 *
 * This software licensed under BSD license, the text of which follows:
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 * - Neither the name of the MontaVista Software, Inc. nor the names of its
 *   contributors may be used to endorse or promote products derived from this
 *   software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */
/*
 * Overloads the ckpt system with checkpoints (30k checkpoints) and then exits
 * ungracefully.  This will cause the entire system to go into overload
 * as it sends out close messages for the 30k open checkpoints.
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/un.h>
#include <time.h>

#include "saAis.h"
#include "saCkpt.h"
#include "sa_error.h"

#define SECONDS_TO_EXPIRE 500

int ckptinv;
void printSaNameT (SaNameT *name)
{
	int i;

	for (i = 0; i < name->length; i++) {
		printf ("%c", name->value[i]);
	}
}

SaVersionT version = { 'B', 1, 1 };

SaNameT checkpointName = { 16, "checkpoint-sync\0" };

SaCkptCheckpointCreationAttributesT checkpointCreationAttributes = {
	.creationFlags =        SA_CKPT_WR_ALL_REPLICAS,
	.checkpointSize =       250000,
	.retentionDuration =    SA_TIME_ONE_SECOND * 60,
	.maxSections =          1,
	.maxSectionSize =       250000,
	.maxSectionIdSize =     10
};

char readBuffer1[1025];

SaCkptIOVectorElementT ReadVectorElements[] = {
	{
		SA_CKPT_DEFAULT_SECTION_ID,	
		readBuffer1,
		sizeof (readBuffer1),
		0, 
		0
	}
};

#define DATASIZE 127000
char data[DATASIZE];
SaCkptIOVectorElementT WriteVectorElements[] = {
	{
		SA_CKPT_DEFAULT_SECTION_ID,
		data, /*"written data #1, this should extend past end of old section data", */
		DATASIZE, /*sizeof ("data #1, this should extend past end of old section data") + 1, */
		0, //5, 
		0
	}
};

SaCkptCallbacksT callbacks = {
 	0,
	0
};

#define MAX_DATA_SIZE 100

int main (void) {
	SaCkptHandleT ckptHandle;
	SaCkptCheckpointHandleT checkpointHandle;
	SaAisErrorT error;
	char data[MAX_DATA_SIZE];
	SaCkptIOVectorElementT writeElement;
	SaUint32T erroroneousVectorIndex = 0;
	int i;

	error = saCkptInitialize (&ckptHandle, &callbacks, &version);
	printf ("%s: CkptInitialize\n",
		get_test_output (error, SA_AIS_OK));

	for (i = 0; i < 30000; i++) {
		checkpointName.length =
		sprintf((char*)checkpointName.value, "ckp%05d",i);

		do {
		error = saCkptCheckpointOpen (ckptHandle,
				&checkpointName,
				&checkpointCreationAttributes,
				SA_CKPT_CHECKPOINT_CREATE|SA_CKPT_CHECKPOINT_READ|SA_CKPT_CHECKPOINT_WRITE,
			0,
			&checkpointHandle);
		} while (error == SA_AIS_ERR_TRY_AGAIN);

		sprintf((char*)&data, "%04d", i);
		writeElement.sectionId = (SaCkptSectionIdT)SA_CKPT_DEFAULT_SECTION_ID;
		writeElement.dataBuffer = data;
		writeElement.dataSize = strlen (data) + 1;
		writeElement.dataOffset = 0;
		writeElement.readSize = 0;

		do {
			error = saCkptCheckpointWrite (checkpointHandle,
				&writeElement,
				1,
				&erroroneousVectorIndex);

		} while (error == SA_AIS_ERR_TRY_AGAIN);

	}

	return (0);

}