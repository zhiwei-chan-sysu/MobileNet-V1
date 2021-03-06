/****************************************************************************
 *               University of North Carolina Charlotte                     *
 *                        MobileNet V1 CNN                                  *
 *                        				                                    *
 *                                                                          *
 *                                                                          *
 *   Author:    1. Kaustubh Manohar Mhatre                                  *
 *              2. Ushma Bharucha                                           *
 *   Date: 08 June 2019														*
 ****************************************************************************/

/****************************************************************************
* Includes																	*
*****************************************************************************/
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <CL/cl.h>
#include <stdbool.h>
#include "layerdef.h"
#define CL_USE_DEPRECATED_OPENCL_1_2_APIS
#define NPY_COMMON_HEADER_OFFSET		10 //offset to the Size of Header in npy file (npy - numpy array file) 
unsigned char image[HEIGHT_0 * WIDTH_0 * FDIM]; //image with 3 input channels
float* filter;
float* filter_proper;
float* gama;
float* beta;
float* moving_mean;
float* variance;
int err;
int layer_count = 0;

cl_device_id device_id;             // compute device id 
cl_context context;                 // compute context
cl_command_queue commands;          // compute command queue
cl_program program;                 // compute program
cl_kernel standard_conv;            // compute kernel for standard convolution
cl_kernel depthwise_conv;           // compute kernel for depthwise convolution
cl_kernel pointwise_conv;           // compute kernel for pointwise convolution
cl_kernel avgPool;					// compute kernel for average pool

cl_mem d_filter; 					//filter
cl_mem d_output; 					//output image
cl_event myevent; 					//profiling event
cl_ulong start; 					//time start
cl_ulong end; 						//time stop
cl_float kernelExecTimeNs;
cl_uint dev_cnt = 0;
cl_platform_id platform_ids[100];


int decode_image(unsigned char frame[HEIGHT_0 * WIDTH_0 * FDIM], char filename[]);
void getBias(float* f, char filename[], int size);
void getWeights(float* aryWeight, char filename[], int size);
void arrangWeights(float* ip, float* op);
void arrangWeightsDepthwise(float* ip, float* op, int fsize);
void arrangWeightsPointwise(float* ip, float* op, int fsize, int totalFilter);

long LoadOpenCLKernel(char const* path, char **buf)
{
	FILE  *fp;
	size_t fsz;
	long   off_end;
	int    rc;

	/* Open the file */
	fp = fopen(path, "r");
	if( NULL == fp ) {
		return -1L;
	}

	/* Seek to the end of the file */
	rc = fseek(fp, 0L, SEEK_END);
	if( 0 != rc ) {
		return -1L;
	}

	/* Byte offset to the end of the file (size) */
	if( 0 > (off_end = ftell(fp)) ) {
		return -1L;
	}
	fsz = (size_t)off_end;

	/* Allocate a buffer to hold the whole file */
	*buf = (char *) malloc( fsz+1);
	if( NULL == *buf ) {
		return -1L;
	}

	/* Rewind file pointer to start of file */
	rewind(fp);

	/* Slurp file into buffer */
	if( fsz != fread(*buf, 1, fsz, fp) ) {
		free(*buf);
		return -1L;
	}

	/* Close the file */
	if( EOF == fclose(fp) ) {
		free(*buf);
		return -1L;
	}


	/* Make sure the buffer is NUL-terminated, just in case */
	(*buf)[fsz] = '\0';

	/* Return the file size */
	return (long)fsz;
}

int openClDeviceConfig(){

	printf("Initializing OpenCL device...\n"); 

	clGetPlatformIDs(0, 0, &dev_cnt);
	clGetPlatformIDs(dev_cnt, platform_ids, NULL);
	
	// Connect to a compute device
	int gpu = 1;
	err = clGetDeviceIDs(platform_ids[0], gpu ? CL_DEVICE_TYPE_GPU : CL_DEVICE_TYPE_CPU, 1, &device_id, NULL);
	if (err != CL_SUCCESS)
	{
		printf("Error: Failed to create a device group!\n");
		return EXIT_FAILURE;
	}

}

int openClCreateContext() {
	// Create a compute context 
	context = clCreateContext(0, 1, &device_id, NULL, NULL, &err);
	if (!context)
	{
		printf("Error: Failed to create a compute context!\n");
		return EXIT_FAILURE;
	}

	// Create a command commands
	commands = clCreateCommandQueue(context, device_id, CL_QUEUE_PROFILING_ENABLE, &err);
	if (!commands)
	{
		printf("Error: Failed to create a command commands!\n");
		return EXIT_FAILURE;
	}
}

int openClCreateKernel() {
	
	// Create the compute program from the source file
	char *KernelSource;
	long lFileSize;

	lFileSize = LoadOpenCLKernel("kernel.cl", &KernelSource);
	if( lFileSize < 0L ) {
		perror("File read failed");
		return 1;
	}

	program = clCreateProgramWithSource(context, 1, (const char **) &KernelSource, NULL, &err);
	if (!program)
	{
		printf("Error: Failed to create compute program!\n");
		return EXIT_FAILURE;
	}

	// Build the program executable
	err = clBuildProgram(program, 0, NULL, NULL, NULL, NULL);
	if (err != CL_SUCCESS)
	{
		size_t len;
		char buffer[2048];
		printf("Error: Failed to build program executable!\n");
		clGetProgramBuildInfo(program, device_id, CL_PROGRAM_BUILD_LOG, sizeof(buffer), buffer, &len);
		printf("%s\n", buffer);
		exit(1);
	}

	// Create the compute kernel for standard convolution
	standard_conv = clCreateKernel(program, "convolute", &err);
	if (!standard_conv || err != CL_SUCCESS)
	{
		printf("Error: Failed to create compute kernel!\n");
		exit(1);
	}

	// Create the compute kernel for depthwise convolution
	depthwise_conv = clCreateKernel(program, "depthwise", &err);
	if (!depthwise_conv || err != CL_SUCCESS)
	{
		printf("Error: Failed to create compute kernel!\n");
		exit(1);
	}

	// Create the compute kernel for Pointwise
	pointwise_conv = clCreateKernel(program, "pointwise", &err);
	if (!pointwise_conv || err != CL_SUCCESS)
	{
		printf("Error: Failed to create compute kernel!\n");
		exit(1);
	}
		
	// Create the compute kernel for average pool
	avgPool = clCreateKernel(program, "avgPool", &err);
	if (!avgPool || err != CL_SUCCESS)
	{
		printf("Error: Failed to create compute kernel!\n");
		exit(1);
	}
}

void seperateChannels(unsigned char* imd, unsigned char* im1, unsigned char* im2, unsigned char* im3){
    int i,j;    
    for(i=0, j=0; i<HEIGHT_0*WIDTH_0; i++, j+=3){
        im1[i] = imd[j];
        im2[i] = imd[j+1];
        im3[i] = imd[j+2];                
    }
}

void uintToFloat(unsigned char* im, float* imf) {
	int i;
	float offset = 127.5;
	for (i = 0; i < HEIGHT_0 * WIDTH_0; i++) {
		imf[i] = (im[i] / offset) - 1;
	}
}

/**
 * @brief  Get the weights from the numpy array file
 * @author  Kausutbh
 * @date July 4, 2019
 * @param 1. float* f : variable to put weights into
 *        2. char filename[] : File name of the weights filename
 *        3. int size
 * @return None
 */
void getWeights(float* aryWeight, char filename[], int size)
{
    FILE *npyfile;
    uint16_t headerOffset;  
	npyfile=fopen(filename,"r");
    fseek(npyfile, 8, SEEK_SET);
    fread(&headerOffset,sizeof(uint16_t),1,npyfile);
    //printf("shift headerOffset - %d \n", headerOffset);    
    fseek(npyfile, ( headerOffset + NPY_COMMON_HEADER_OFFSET ), SEEK_SET);
    fread(aryWeight,sizeof(float),size,npyfile);
    fclose(npyfile);
}
/**
 * @brief  Get the bias from the numpy array file
 * @author  Kausutbh
 * @date June 20, 2019
 * @param 1. int* f : variable to put weights into
 *        2. char filename[] : File name of the weights filename
 *        3. int size
 * @return None
 */
void getBias(float* f, char filename[], int size)
{
    FILE *latfile;
    latfile=fopen(filename,"r");
    /* 80 is the offset of numpy array file*/
    fseek(latfile, 80, SEEK_SET);
    fread(f,sizeof(int),size,latfile);
    fclose(latfile);
}
//Function to read image files in C
int decode_image(unsigned char frame[HEIGHT_0 * WIDTH_0 * FDIM],char filename[])
{
	FILE *pFile;
	pFile = fopen(filename, "r"); //read mode
	fseek(pFile, 15, SEEK_SET);
	fread(frame, sizeof(unsigned char), HEIGHT_0 * WIDTH_0 * FDIM, pFile);
	fclose(pFile);
	return 0;
}


void display_data(unsigned char* data,int num) {
	int i,j;
	for (j = 0 ;j < num ; j++){
		for(i = 0; i < num; i++){
			printf("%d\t", data[j*WIDTH_0+i]);
		}
		printf("\n");
	}
	printf("\n");
}

void convStandard (float* opfm) {

	cl_mem d_image_r; //R channel
	cl_mem d_image_g; //G channel
	cl_mem d_image_b; //B channel

	unsigned char* image_r = (unsigned char*) malloc(HEIGHT_0 * WIDTH_0 * sizeof(unsigned char)); //R channel
	unsigned char* image_g = (unsigned char*) malloc(HEIGHT_0 * WIDTH_0 * sizeof(unsigned char)); //G channel
	unsigned char* image_b = (unsigned char*) malloc(HEIGHT_0 * WIDTH_0 * sizeof(unsigned char)); //B channel

	//Image data in float
	float* image_r_f = (float*) malloc(HEIGHT_0 * WIDTH_0 * sizeof(float)); //R channel in float
	float* image_g_f = (float*) malloc(HEIGHT_0 * WIDTH_0 * sizeof(float)); //G channel in float
	float* image_b_f = (float*) malloc(HEIGHT_0 * WIDTH_0 * sizeof(float)); //B channel in float

	int i,j,k;

	//Read pixel values from input image
	decode_image(image,"testData/tiger.ppm"); 
	//separate R,G and B pixels
	seperateChannels(image, image_r, image_g, image_b);

	//Convert uint8 image data to float
	uintToFloat(image_r, image_r_f);
	uintToFloat(image_g, image_g_f);
	uintToFloat(image_b, image_b_f);

	//Get filter values
    getWeights(filter,"weights_float/conv1_kernel_0",(IP_FM_1*FDIM*FDIM*FDIM));

	//Get beta, gama, variance and mooving mean
	getWeights(gama, "gamma/conv1_bn_gamma_0", IP_FM_1);					//gamma
	getWeights(beta, "beta/conv1_bn_beta_0", IP_FM_1);						//beta
	getWeights(moving_mean, "mean/conv1_bn_moving_mean_0", IP_FM_1);		//moving_mean
	getWeights(variance, "variance/conv1_bn_moving_variance_0", IP_FM_1);	//variance

	//reaarange weights in proper format
	arrangWeights(filter, filter_proper);

	//Create buffer for device
	d_image_r = clCreateBuffer(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, HEIGHT_0*WIDTH_0*sizeof(float), image_r_f, &err);
	d_image_g = clCreateBuffer(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, HEIGHT_0*WIDTH_0*sizeof(float), image_g_f, &err);
	d_image_b = clCreateBuffer(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, HEIGHT_0*WIDTH_0*sizeof(float), image_b_f, &err);
	d_output = clCreateBuffer(context, CL_MEM_WRITE_ONLY, (HEIGHT_1)*(WIDTH_1)*IP_FM_1*sizeof(float), NULL, &err);
	d_filter = clCreateBuffer(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, IP_FM_1*FDIM*FDIM*FDIM*sizeof(float), filter, &err);

	if (!d_image_r || !d_image_g || !d_image_b || !d_filter || !d_output)
	{
		printf("Error: Failed to allocate device memory!\n");
		exit(1);
	}    
	
	err = clEnqueueWriteBuffer(commands, d_image_r, CL_TRUE, 0, HEIGHT_0*WIDTH_0*sizeof(float), image_r_f, 0, NULL, NULL);
	err |= clEnqueueWriteBuffer(commands, d_image_g, CL_TRUE, 0, HEIGHT_0*WIDTH_0*sizeof(float), image_g_f, 0, NULL, NULL);   
	err |= clEnqueueWriteBuffer(commands, d_image_b, CL_TRUE, 0, HEIGHT_0*WIDTH_0*sizeof(float), image_b_f, 0, NULL, NULL);   
	err |= clEnqueueWriteBuffer(commands, d_filter, CL_TRUE, 0, IP_FM_1*FDIM*FDIM*FDIM*sizeof(float), filter_proper, 0, NULL, NULL);   

	if (err != CL_SUCCESS)
	{
		printf("Error: Failed to write data to device! %d\n", err);
		exit(1);
	}
 
	int rows = HEIGHT_0;
	int cols = WIDTH_0;
	int filtersize = FDIM;
	int no_fm_0 = OP_FM_0;
    int stride = 2;

	err = clSetKernelArg(standard_conv, 0, sizeof(cl_mem), (void *)&d_output);
	err |= clSetKernelArg(standard_conv, 1, sizeof(cl_mem), (void *)&d_image_r);
	err |= clSetKernelArg(standard_conv, 2, sizeof(cl_mem), (void *)&d_image_g);
	err |= clSetKernelArg(standard_conv, 3, sizeof(cl_mem), (void *)&d_image_b);
	err |= clSetKernelArg(standard_conv, 4, sizeof(cl_mem), (void *)&d_filter);
	err |= clSetKernelArg(standard_conv, 5, sizeof(int), (void *)&rows);
	err |= clSetKernelArg(standard_conv, 6, sizeof(int), (void *)&cols);
	err |= clSetKernelArg(standard_conv, 7, sizeof(int), (void *)&filtersize);
    err |= clSetKernelArg(standard_conv, 8, sizeof(int), (void *)&stride);
    err |= clSetKernelArg(standard_conv, 9, sizeof(int), (void *)&no_fm_0);


	if (err != CL_SUCCESS)
	{ 
		printf("Error: Failed to set kernel arguments! %d\n", err);
		exit(1);
	}
	
	size_t localWorkSize[2], globalWorkSize[2];
	localWorkSize[0] = 8;
	localWorkSize[1] = 8;
	globalWorkSize[0] = 112;
	globalWorkSize[1] = 112;
	err = clEnqueueNDRangeKernel(commands, standard_conv, 2, NULL, globalWorkSize, localWorkSize, 0, NULL, &myevent);   
    
	if (err != CL_SUCCESS)
	{
		printf("Error: Failed to execute kernel! %d\n", err);
		exit(1);
	}
   
	clWaitForEvents(1,&myevent);	 
	clFinish(commands);   
	clGetEventProfilingInfo(myevent,CL_PROFILING_COMMAND_START, sizeof(cl_ulong), &start, NULL);
	clGetEventProfilingInfo(myevent,CL_PROFILING_COMMAND_END,sizeof(cl_ulong), &end, NULL);
	kernelExecTimeNs += end - start;
	err = clEnqueueReadBuffer(commands, d_output, CL_TRUE, 0, IP_FM_1*(HEIGHT_1)*(WIDTH_1)*sizeof(float), opfm, 0, NULL, NULL);

	printf("Before BN Data for layer %d\n", layer_count);
	 
	// for (k = 0; k < 32; k++){
	// 	for (j = 111; j < 112; j++){
	// 		for(i = 0; i < 112; i++){
	// 			printf("%e\t", opfm[(j*112+i) + (k*112*112)]);
	// 		}
	// 		printf("\n");
	// 	}
    // printf("\n");
	// }

	//Batch Normalization of output data
	for (k = 0; k < IP_FM_1; k++) {
		for (j = 0; j < HEIGHT_1 * WIDTH_1; j++){
			opfm[j + (k * HEIGHT_1 * WIDTH_1)] =  (gama[k] * ((opfm[j + (k * HEIGHT_1 * WIDTH_1)] - moving_mean[k]) / sqrt(variance[k] + 0.001))) + beta[k];
			if (opfm[j + (k * HEIGHT_1 * WIDTH_1)] <= 0){
				opfm[j + (k * HEIGHT_1 * WIDTH_1)] = 0;
			}
			else if (opfm[j + (k * HEIGHT_1 * WIDTH_1)] > 6){
				opfm[j + (k * HEIGHT_1 * WIDTH_1)] = 6;
			}
		}
		//printf("\n");
	}


	if (err != CL_SUCCESS)
	{
		printf("Error: Failed to read output array! %d\n", err);
		exit(1);
	}
     
	//Get kernel execution time
	printf("Kernel Execution time for Layer %d: %f\n", layer_count, kernelExecTimeNs/1000000000);

	// printf("Data for Layer %d\n", layer_count);
	 
	// for (k = 0; k < 32; k++){
	// 	for (j = 0; j < 12; j++){
	// 		for(i = 0; i < 12; i++){
	// 			printf("%f\t", opfm[(j*112+i) + (k*112*112)]);
	// 		}
	// 		printf("\n");
	// 	}
    // printf("\n");
	// }	
	
	free(image_r);
	free(image_g);
	free(image_b);

	clReleaseMemObject(d_image_r);
	clReleaseMemObject(d_image_g);
	clReleaseMemObject(d_image_b);

}

void convDepthwise(float* ipfm, float* opfm, char* fileName_gama, char* fileName_beta, char* fileName_mean, char* fileName_variance, 
				   char* fileName_filter, int iph, int ipw, int oph, int opw, int ip_fsize, 
				   int op_fsize, int stride) {
	
	cl_mem d_input;	//Input Data

	kernelExecTimeNs = 0;
	int i,j,k;

	//Get filter values
	getWeights(filter,fileName_filter,(op_fsize*FDIM*FDIM));

	//Get beta, gama, variance and mooving mean
	getWeights(gama, fileName_gama, op_fsize);				//gamma
	getWeights(beta, fileName_beta, op_fsize);				//beta
	getWeights(moving_mean, fileName_mean, op_fsize);		//moving_mean
	getWeights(variance, fileName_variance, op_fsize);		//variance

	//reaarange weights in proper format
	arrangWeightsDepthwise(filter, filter_proper, op_fsize);
	
	//Create buffer for device
	d_input = clCreateBuffer(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, iph*ipw*ip_fsize*sizeof(float), ipfm, &err);
	d_output = clCreateBuffer(context, CL_MEM_WRITE_ONLY, oph*opw*op_fsize*sizeof(float), NULL, &err);
	d_filter = clCreateBuffer(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, op_fsize*FDIM*FDIM*sizeof(float), filter, &err);	

	if (!d_input || !d_filter || !d_output)
	{
		printf("Error: Failed to allocate device memory!\n");
		exit(1);
	}    
	
	err = clEnqueueWriteBuffer(commands, d_input, CL_TRUE, 0, iph*ipw*ip_fsize*sizeof(float), ipfm, 0, NULL, NULL);
	err |= clEnqueueWriteBuffer(commands, d_filter, CL_TRUE, 0, op_fsize*FDIM*FDIM*sizeof(float), filter_proper, 0, NULL, NULL);

	if (err != CL_SUCCESS)
	{
		printf("Error: Failed to write data to device! %d\n", err);
		exit(1);
	}
 
	int rows = iph;
	int cols = ipw;
	int filtersize = FDIM;
    
	err = clSetKernelArg(depthwise_conv, 0, sizeof(cl_mem), (void *)&d_output);
	err |= clSetKernelArg(depthwise_conv, 1, sizeof(cl_mem), (void *)&d_input);
	err |= clSetKernelArg(depthwise_conv, 2, sizeof(cl_mem), (void *)&d_filter);
	err |= clSetKernelArg(depthwise_conv, 3, sizeof(int), (void *)&rows);
	err |= clSetKernelArg(depthwise_conv, 4, sizeof(int), (void *)&cols);
	err |= clSetKernelArg(depthwise_conv, 5, sizeof(int), (void *)&filtersize);
	err |= clSetKernelArg(depthwise_conv, 6, sizeof(int), (void *)&stride);
	err |= clSetKernelArg(depthwise_conv, 7, sizeof(int), (void *)&op_fsize);
    
	if (err != CL_SUCCESS)
	{ 
		printf("Error: Failed to set kernel arguments! %d\n", err);
		exit(1);
	}

	size_t localWorkSize[2], globalWorkSize[2];
	localWorkSize[0] = 1;
	localWorkSize[1] = 1;
	globalWorkSize[0] = opw;
	globalWorkSize[1] = oph;

	err = clEnqueueNDRangeKernel(commands, depthwise_conv, 2, NULL, globalWorkSize, localWorkSize, 0, NULL, &myevent);   
    
	if (err != CL_SUCCESS)
	{
		printf("Error: Failed to execute kernel! %d\n", err);
		exit(1);
	}
   
	clWaitForEvents(1,&myevent);	 
	clFinish(commands);   
	clGetEventProfilingInfo(myevent, CL_PROFILING_COMMAND_START, sizeof(cl_ulong), &start, NULL);
	clGetEventProfilingInfo(myevent, CL_PROFILING_COMMAND_END, sizeof(cl_ulong), &end, NULL);
	kernelExecTimeNs += end - start;	
	err = clEnqueueReadBuffer(commands, d_output, CL_TRUE, 0, op_fsize*oph*opw*sizeof(float), opfm, 0, NULL, NULL);

	if (err != CL_SUCCESS)
	{
		printf("Error: Failed to read output array! %d\n", err);
		exit(1);
	}

	// printf("Before BN data for Layer %d\n", layer_count);

	// for (k = 0; k < op_fsize; k++){
	// 	for (j = 111; j < 112; j++){
	// 		for(i = 0; i < 112; i++){
	// 			printf("%e\t", opfm[(j*opw+i) + (k*oph*opw)]);
	// 		}
	// 		printf("\n");
	// 	}
    // printf("\n");
	// }

	//Batch Normalization of output data
	for (k = 0; k < op_fsize; k++) {
		for (j = 0; j < oph * opw; j++){
			opfm[j + (k * oph * opw)] =  (gama[k] * ((opfm[j + (k * oph * opw)] - moving_mean[k]) / sqrt(variance[k] + 0.001))) + beta[k];
			if (opfm[j + (k * oph * opw)] <= 0 ){
				opfm[j + (k * oph * opw)] = 0;
			}
			else if (opfm[j + (k * oph * opw)] > 6 ){
				opfm[j + (k * oph * opw)] = 6;
			}
		}
		//printf("\n");
	}

	printf("Kernel Execution time for Layer %d: %f\n", layer_count, kernelExecTimeNs/1000000000);

	printf("Data for Layer %d\n", layer_count);

	// for (k = 0; k < op_fsize; k++){
	// 	for (j = 111; j < 112; j++){
	// 		for(i = 0; i < 112; i++){
	// 			printf("%e\t", opfm[(j*opw+i) + (k*oph*opw)]);
	// 		}
	// 		printf("\n");
	// 	}
    // printf("\n");
	// }
	
	clReleaseMemObject(d_input);

}

void convPointwise(float* ipfm, float* opfm, char* fileName_gama, char* fileName_beta, char* fileName_mean, char* fileName_variance,
					char* fileName_filter, int iph, int ipw, int oph, int opw, int ip_fsize, int op_fsize) {

	cl_mem d_input;	//Input Data
	
	kernelExecTimeNs = 0;
	int i,j,k;

	//Get filter values
	getWeights(filter, fileName_filter, (ip_fsize*op_fsize*FDIM_P*FDIM_P));

	//Get beta, gama, variance and mooving mean
	getWeights(gama, fileName_gama, op_fsize);				//gamma
	getWeights(beta, fileName_beta, op_fsize);				//beta
	getWeights(moving_mean, fileName_mean, op_fsize);		//moving_mean
	getWeights(variance, fileName_variance, op_fsize);		//variance

	//reaarange weights in proper format
	arrangWeightsPointwise(filter, filter_proper, ip_fsize, op_fsize);
	
	//Create buffer for device
	d_input = clCreateBuffer(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, iph*ipw*ip_fsize*sizeof(float), ipfm, &err);
	d_output = clCreateBuffer(context, CL_MEM_WRITE_ONLY, oph*opw*op_fsize*sizeof(float), NULL, &err);
	d_filter = clCreateBuffer(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, ip_fsize*op_fsize*FDIM_P*sizeof(float), filter, &err);
	
	if (!d_input || !d_filter || !d_output)
	{
		printf("Error: Failed to allocate device memory!\n");
		exit(1);
	}
	
	err = clEnqueueWriteBuffer(commands, d_input, CL_TRUE, 0, iph*ipw*ip_fsize*sizeof(float), ipfm, 0, NULL, NULL);
	err |= clEnqueueWriteBuffer(commands, d_filter, CL_TRUE, 0, ip_fsize*op_fsize*FDIM_P*FDIM_P*sizeof(float), filter_proper, 0, NULL, NULL);

	if (err != CL_SUCCESS)
	{
		printf("Error: Failed to write data to device! %d\n", err);
		exit(1);
	}
 
	int rows = iph;
	int cols = ipw;
	int filtersize = ip_fsize;
    
	err = clSetKernelArg(pointwise_conv, 0, sizeof(cl_mem), (void *)&d_output);
	err |= clSetKernelArg(pointwise_conv, 1, sizeof(cl_mem), (void *)&d_input);
	err |= clSetKernelArg(pointwise_conv, 2, sizeof(cl_mem), (void *)&d_filter);
	err |= clSetKernelArg(pointwise_conv, 3, sizeof(int), (void *)&rows);
	err |= clSetKernelArg(pointwise_conv, 4, sizeof(int), (void *)&cols);
	err |= clSetKernelArg(pointwise_conv, 5, sizeof(int), (void *)&filtersize);
	err |= clSetKernelArg(pointwise_conv, 6, sizeof(int), (void *)&op_fsize);
	
	if (err != CL_SUCCESS)
	{ 
		printf("Error: Failed to set kernel arguments! %d\n", err);
		exit(1);
	}

	size_t localWorkSize[2], globalWorkSize[2];
	localWorkSize[0] = 1;
	localWorkSize[1] = 1;
	globalWorkSize[0] = opw;
	globalWorkSize[1] = oph;
	err = clEnqueueNDRangeKernel(commands, pointwise_conv, 2, NULL, globalWorkSize, localWorkSize, 0, NULL, &myevent);   
    
	if (err != CL_SUCCESS)
	{
		printf("Error: Failed to execute kernel! %d\n", err);
		exit(1);
	}
   
	clWaitForEvents(1,&myevent);	 
	clFinish(commands);   
	clGetEventProfilingInfo(myevent,CL_PROFILING_COMMAND_START, sizeof(cl_ulong), &start, NULL);
	clGetEventProfilingInfo(myevent,CL_PROFILING_COMMAND_END,sizeof(cl_ulong), &end, NULL);
	kernelExecTimeNs += end - start;
	err = clEnqueueReadBuffer(commands, d_output, CL_TRUE, 0, op_fsize*oph*opw*sizeof(float), opfm, 0, NULL, NULL);

	if (err != CL_SUCCESS)
	{
		printf("Error: Failed to read output array! %d\n", err);
		exit(1);
	}

	//Batch Normalization of output data
	for (k = 0; k < op_fsize; k++) {
		for (j = 0; j < oph * opw; j++){
			opfm[j + (k * oph * opw)] =  (gama[k] * ((opfm[j + (k * oph * opw)] - moving_mean[k]) / sqrt(variance[k] + 0.001))) + beta[k];
			if (opfm[j + (k * oph * opw)] <= 0 ){
				opfm[j + (k * oph * opw)] = 0;
			}
			else if (opfm[j + (k * oph * opw)] > 6 ){
				opfm[j + (k * oph * opw)] = 6;
			}
		}
		//printf("\n");
	}

	//Get kernel execution time
	printf("Kernel Execution time for Layer %d: %f\n", layer_count, kernelExecTimeNs/1000000000);

	// printf("Pointwise After BN data for Layer %d\n", layer_count);

	// for (k = 0; k < op_fsize; k++){
	// 	for (j = 0; j < oph; j++){
	// 		for(i = 0; i < opw; i++){
	// 			printf("%e\t", opfm[(j*opw+i) + (k*oph*opw)]);
	// 		}
	// 		printf("\n");
	// 	}
    // printf("\n");
	// }

	clReleaseMemObject(d_input);
}

void convAvgPool(float* ipfm, float* opfm, int iph, int ipw, 
				int oph, int opw, int ip_fsize, int op_fsize) {

	cl_mem d_input;	//Input Data	

	kernelExecTimeNs = 0;
	int i,j,k;

	//Create buffer for device
	d_input = clCreateBuffer(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, iph*ipw*ip_fsize*sizeof(float), ipfm, &err);
	d_output = clCreateBuffer(context, CL_MEM_WRITE_ONLY, oph*opw*op_fsize*sizeof(float), NULL, &err);

	if (!d_input || !d_output )
	{
		printf("Error: Failed to allocate device memory!\n");
		exit(1);
	}
	
	err = clEnqueueWriteBuffer(commands, d_input, CL_TRUE, 0, iph*ipw*ip_fsize*sizeof(float), ipfm, 0, NULL, NULL);

	if (err != CL_SUCCESS)
	{
		printf("Error: Failed to write data to device! %d\n", err);
		exit(1);
	}
 
	int rows = iph;
	int cols = ipw;
    
	err = clSetKernelArg(avgPool, 0, sizeof(cl_mem), (void *)&d_output);
	err |= clSetKernelArg(avgPool, 1, sizeof(cl_mem), (void *)&d_input);
	err |= clSetKernelArg(avgPool, 2, sizeof(int), (void *)&rows);
	err |= clSetKernelArg(avgPool, 3, sizeof(int), (void *)&cols);

	if (err != CL_SUCCESS)
	{ 
		printf("Error: Failed to set kernel arguments! %d\n", err);
		exit(1);
	}

	size_t localWorkSize[1], globalWorkSize[1];
	localWorkSize[0] = 16;
	globalWorkSize[0] = op_fsize;
	err = clEnqueueNDRangeKernel(commands, avgPool, 1, NULL, globalWorkSize, localWorkSize, 0, NULL, &myevent);   
    
	if (err != CL_SUCCESS)
	{
		printf("Error: Failed to execute kernel! %d\n", err);
		exit(1);
	}
   
	clWaitForEvents(1,&myevent);	 
	clFinish(commands);   
	clGetEventProfilingInfo(myevent,CL_PROFILING_COMMAND_START, sizeof(cl_ulong), &start, NULL);
	clGetEventProfilingInfo(myevent,CL_PROFILING_COMMAND_END,sizeof(cl_ulong), &end, NULL);
	kernelExecTimeNs += end - start;
	err = clEnqueueReadBuffer(commands, d_output, CL_TRUE, 0, op_fsize*oph*opw*sizeof(float), opfm, 0, NULL, NULL);

	if (err != CL_SUCCESS)
	{
		printf("Error: Failed to read output array! %d\n", err);
		exit(1);
	}

	//Get kernel execution time
	printf("Kernel Execution time for Layer %d: %f\n", layer_count, kernelExecTimeNs/1000000000);
	 
	/* printf("Avg pool Layer\n");

	for (k = 0; k < 32; k++){
		for (j = 0; j < 5; j++){
			for(i = 0; i < 5; i++){
				printf("%u\t", opfm[(j*112+i) + k]);
			}
			printf("\n");
		}
    	printf("\n");
	}	*/
	clReleaseMemObject(d_input);
}

void fullyConectedLayer( float* ipfm, float* opfm, char* fileName_bias , char* fileName_filter , int classes , int elements)
{   
    int i,j;
	float sum = 0;
	/*Bias*/
	float* h_bias;
	
    h_bias = (float*)malloc(sizeof(float) * classes);

	//Get bias values
    getBias(h_bias, fileName_bias, classes);

	//Get filter values
	getWeights(filter, fileName_filter, (classes * elements));

	//reaarange weights in proper format
	arrangWeightsPointwise(filter, filter_proper, elements, classes);
	printf("\n");
    for(i = 0; i < CLASSES; i++)
    {
        for(j = 0; j < ELEMENTS; j++)
        {
			//printf("Image data %f\t filter data %f\t Multiplication %f\n",ipfm[j], filter_proper[j + (ELEMENTS * i)], ipfm[j] * filter_proper[j + (ELEMENTS * i)]);
		    sum += ipfm[j] * filter_proper[j + (ELEMENTS * i)];
		
			// if (j == 0)
			// 	printf("ip %d + fil %d = sum %d \n", ipfm[j],(filter[j] - Z2_28), sum );
        }
		opfm[i] = sum + h_bias[i];
		sum = 0;
    }
	printf("\n");
    printf("Layer 29 Fully Connected Done\n");
}

//Softmax
void softmax (float* ipfm)
{
    float expo[1000], sum, max = 0.0;
	int maxIndex;
    int i,j;
	int temp;
	printf("SOFTMAX OP: ");
    for(i = 0; i < CLASSES_SOFTMAX; i++)
    {
        expo[i] = exp(ipfm[i]);
        sum += expo[i];
		//printf("i = %d \t ipfm = %f\t%f\n", i, ipfm[i], expo[i]);
    }
	printf("Sum = %f\n", sum);
    for(i = 0; i < CLASSES_SOFTMAX; i++)
    {
		expo[i] = expo[i] / sum;
		//printf("%f\t", expo[i]);
    }
	for(i = 0; i < CLASSES_SOFTMAX; i++)
    {
		if ( expo[i] > max){
			max = expo[i];
			maxIndex = i;
		}	
    }
    printf("Layer 30 softmax Done\n");
	printf("Prediction - %d\t %f\n", maxIndex , max);
}

/**
 * @brief  reaarange the weights in the format required by the kernel
 * @author  Kausutbh
 * @date July 4, 2019
 * @param 1. float* ip
 *        2. float* op
 * @return None
 */
void arrangWeights(float* ip, float* op)
{
    int nof, channel,ele_per_filter,i=0;
    for (nof=0; nof < 32 ; nof++)
    {
        for(ele_per_filter=0;ele_per_filter<9;ele_per_filter++,i++)
        {
            op[i]=ip[0+(ele_per_filter*96)+nof];   
        }
        for(ele_per_filter=0;ele_per_filter<9;ele_per_filter++,i++)
        {
            op[i]=ip[32+(ele_per_filter*96)+nof];
        }
        for(ele_per_filter=0;ele_per_filter<9;ele_per_filter++,i++)
        {
            op[i]=ip[64+(ele_per_filter*96)+nof];
        }
    }

}

void arrangWeightsDepthwise(float* ip, float* op, int fsize)
{
    int nof, channel,ele_per_filter,i=0;
    for (nof=0; nof<fsize; nof++)
    {
        for(ele_per_filter=0;ele_per_filter<9;ele_per_filter++,i++)
        {
            op[i]=ip[0+(ele_per_filter*(fsize))+nof];   
        }
    }
}
void arrangWeightsPointwise(float* ip, float* op, int fsize, int totalFilter)
{
    int nof, channel,ele_per_filter,i=0;
    for (nof=0; nof<totalFilter; nof++)
    {
        for(ele_per_filter=0;ele_per_filter<fsize;ele_per_filter++,i++)
        {
            op[i]=ip[(ele_per_filter*totalFilter)+nof];   
        }
    }
}
//This is the main function
int main(int argc, char** argv) {

    
	filter = (float*) malloc(FILTER_MAX*FILTER_MAX*FDIM*FDIM*FDIM*sizeof(float));
	filter_proper = (float*) malloc(FILTER_MAX*FILTER_MAX*FDIM*FDIM*FDIM*sizeof(float));

	//Need to change
	gama = (float*) malloc(FILTER_MAX*FILTER_MAX*FDIM*FDIM*FDIM*sizeof(float));
	beta = (float*) malloc(FILTER_MAX*FILTER_MAX*FDIM*FDIM*FDIM*sizeof(float));
	moving_mean = (float*) malloc(FILTER_MAX*FILTER_MAX*FDIM*FDIM*FDIM*sizeof(float));
	variance = (float*) malloc(FILTER_MAX*FILTER_MAX*FDIM*FDIM*FDIM*sizeof(float));

	float* op_fm_0 = (float*) malloc(IP_FM_1 * HEIGHT_1 * WIDTH_1 * sizeof(float)); //output feature map for layer 0
	int i,j,k;

	openClDeviceConfig();
	openClCreateContext();
	openClCreateKernel();
	convStandard(op_fm_0); //Layer 0 - Standard Convolution
	
	//Layer 1 Depth-Wise Convolution
	
	layer_count++;
	float* op_fm_1 = (float*) malloc(IP_FM_2 * HEIGHT_2 * WIDTH_2 * sizeof(float)); //output feature map for layer 1
	convDepthwise(op_fm_0, op_fm_1, "gamma/conv_dw_1_bn_gamma_0", "beta/conv_dw_1_bn_beta_0", "mean/conv_dw_1_bn_moving_mean_0", "variance/conv_dw_1_bn_moving_variance_0", "weights_float/conv_dw_1_depthwise_kernel_0", HEIGHT_1, WIDTH_1, HEIGHT_2, WIDTH_2, IP_FM_1, IP_FM_2, 1);
	
	//Layer 2 Point-Wise Convolution

	layer_count++;
	float* op_fm_2 = (float*) malloc(IP_FM_3 * HEIGHT_3 * WIDTH_3 * sizeof(float));	//output feature map for layer 2
	convPointwise(op_fm_1, op_fm_2, "gamma/conv_pw_1_bn_gamma_0", "beta/conv_pw_1_bn_beta_0", "mean/conv_pw_1_bn_moving_mean_0", "variance/conv_pw_1_bn_moving_variance_0", "weights_float/conv_pw_1_kernel_0", HEIGHT_2, WIDTH_2, HEIGHT_3, WIDTH_3, IP_FM_2, IP_FM_3);
 
 	//Layer 3 Depth-Wise Convolution

	layer_count++;
	float* op_fm_3 = (float*) malloc(IP_FM_4 * HEIGHT_4 * WIDTH_4 * sizeof(float)); //output feature map for layer 3
	convDepthwise(op_fm_2, op_fm_3, "gamma/conv_dw_2_bn_gamma_0", "beta/conv_dw_2_bn_beta_0", "mean/conv_dw_2_bn_moving_mean_0", "variance/conv_dw_2_bn_moving_variance_0", "weights_float/conv_dw_2_depthwise_kernel_0", HEIGHT_3, WIDTH_3, HEIGHT_4, WIDTH_4, IP_FM_3, IP_FM_4, 2);

	// for (k = 0; k < IP_FM_4; k++){
	// 	for (j = 0; j < 6; j++){
	// 		for(i = 0; i < 6; i++){
	// 			printf("%f\t", op_fm_3[(j*WIDTH_4+i) + (k*HEIGHT_4*WIDTH_4)]);
	// 		}
	// 		printf("\n");
	// 	}
    // 	printf("\n");
	// }

	//Layer 4 Point-Wise Convolution

	layer_count++;
	float* op_fm_4 = (float*) malloc(IP_FM_5 * HEIGHT_5 * WIDTH_5 * sizeof(float));	//output feature map for layer 4
	convPointwise(op_fm_3, op_fm_4, "gamma/conv_pw_2_bn_gamma_0", "beta/conv_pw_2_bn_beta_0", "mean/conv_pw_2_bn_moving_mean_0", "variance/conv_pw_2_bn_moving_variance_0", "weights_float/conv_pw_2_kernel_0", HEIGHT_4, WIDTH_4, HEIGHT_5, WIDTH_5, IP_FM_4, IP_FM_5);

	//Layer 5 Depth-Wise Convolution

	layer_count++;
	float* op_fm_5 = (float*) malloc(IP_FM_6 * HEIGHT_6 * WIDTH_6 * sizeof(float)); //output feature map for layer 5
	convDepthwise(op_fm_4, op_fm_5, "gamma/conv_dw_3_bn_gamma_0", "beta/conv_dw_3_bn_beta_0", "mean/conv_dw_3_bn_moving_mean_0", "variance/conv_dw_3_bn_moving_variance_0", "weights_float/conv_dw_3_depthwise_kernel_0", HEIGHT_5, WIDTH_5, HEIGHT_6, WIDTH_6, IP_FM_5, IP_FM_6, 1);

	//Layer 6 Point-Wise Convolution

	layer_count++;
	float* op_fm_6 = (float*) malloc(IP_FM_7 * HEIGHT_7 * WIDTH_7 * sizeof(float));	//output feature map for layer 6
	convPointwise(op_fm_5, op_fm_6, "gamma/conv_pw_3_bn_gamma_0", "beta/conv_pw_3_bn_beta_0", "mean/conv_pw_3_bn_moving_mean_0", "variance/conv_pw_3_bn_moving_variance_0", "weights_float/conv_pw_3_kernel_0", HEIGHT_6, WIDTH_6, HEIGHT_7, WIDTH_7, IP_FM_6, IP_FM_7);

	//Layer 7 Depth-Wise Convolution

	layer_count++;
	float* op_fm_7 = (float*) malloc(IP_FM_8 * HEIGHT_8 * WIDTH_8 * sizeof(float)); //output feature map for layer 7
	convDepthwise(op_fm_6, op_fm_7, "gamma/conv_dw_4_bn_gamma_0", "beta/conv_dw_4_bn_beta_0", "mean/conv_dw_4_bn_moving_mean_0", "variance/conv_dw_4_bn_moving_variance_0", "weights_float/conv_dw_4_depthwise_kernel_0", HEIGHT_7, WIDTH_7, HEIGHT_8, WIDTH_8, IP_FM_7, IP_FM_8, 2);

	//Layer 8 Point-Wise Convolution

	layer_count++;
	float* op_fm_8 = (float*) malloc(IP_FM_9 * HEIGHT_9 * WIDTH_9 * sizeof(float));	//output feature map for layer 8
	convPointwise(op_fm_7, op_fm_8, "gamma/conv_pw_4_bn_gamma_0", "beta/conv_pw_4_bn_beta_0", "mean/conv_pw_4_bn_moving_mean_0", "variance/conv_pw_4_bn_moving_variance_0", "weights_float/conv_pw_4_kernel_0", HEIGHT_8, WIDTH_8, HEIGHT_9, WIDTH_9, IP_FM_8, IP_FM_9);

	//Layer 9 Depth-Wise Convolution

	layer_count++;
	float* op_fm_9 = (float*) malloc(IP_FM_10 * HEIGHT_10 * WIDTH_10 * sizeof(float)); //output feature map for layer 9
	convDepthwise(op_fm_8, op_fm_9, "gamma/conv_dw_5_bn_gamma_0", "beta/conv_dw_5_bn_beta_0", "mean/conv_dw_5_bn_moving_mean_0", "variance/conv_dw_5_bn_moving_variance_0", "weights_float/conv_dw_5_depthwise_kernel_0", HEIGHT_9, WIDTH_9, HEIGHT_10, WIDTH_10, IP_FM_9, IP_FM_10, 1);

	//Layer 10 Point-Wise Convolution

	layer_count++;
	float* op_fm_10 = (float*) malloc(IP_FM_11 * HEIGHT_11 * WIDTH_11 * sizeof(float));	//output feature map for layer 10
	convPointwise(op_fm_9, op_fm_10, "gamma/conv_pw_5_bn_gamma_0", "beta/conv_pw_5_bn_beta_0", "mean/conv_pw_5_bn_moving_mean_0", "variance/conv_pw_5_bn_moving_variance_0", "weights_float/conv_pw_5_kernel_0", HEIGHT_10, WIDTH_10, HEIGHT_11, WIDTH_11, IP_FM_10, IP_FM_11);

	//Layer 11 Depth-Wise Convolution

	layer_count++;
	float* op_fm_11 = (float*) malloc(IP_FM_12 * HEIGHT_12 * WIDTH_12 * sizeof(float)); //output feature map for layer 11
	convDepthwise(op_fm_10, op_fm_11, "gamma/conv_dw_6_bn_gamma_0", "beta/conv_dw_6_bn_beta_0", "mean/conv_dw_6_bn_moving_mean_0", "variance/conv_dw_6_bn_moving_variance_0", "weights_float/conv_dw_6_depthwise_kernel_0", HEIGHT_11, WIDTH_11, HEIGHT_12, WIDTH_12, IP_FM_11, IP_FM_12, 2);

	//Layer 12 Point-Wise Convolution

	layer_count++;
	float* op_fm_12 = (float*) malloc(IP_FM_13 * HEIGHT_13 * WIDTH_13 * sizeof(float));	//output feature map for layer 12
	convPointwise(op_fm_11, op_fm_12, "gamma/conv_pw_6_bn_gamma_0", "beta/conv_pw_6_bn_beta_0", "mean/conv_pw_6_bn_moving_mean_0", "variance/conv_pw_6_bn_moving_variance_0", "weights_float/conv_pw_6_kernel_0", HEIGHT_12, WIDTH_12, HEIGHT_13, WIDTH_13, IP_FM_12, IP_FM_13);

	//Layer 13 Depth-Wise Convolution

	layer_count++;
	float* op_fm_13 = (float*) malloc(IP_FM_14 * HEIGHT_14 * WIDTH_14 * sizeof(float)); //output feature map for layer 13
	convDepthwise(op_fm_12, op_fm_13, "gamma/conv_dw_7_bn_gamma_0", "beta/conv_dw_7_bn_beta_0", "mean/conv_dw_7_bn_moving_mean_0", "variance/conv_dw_7_bn_moving_variance_0", "weights_float/conv_dw_7_depthwise_kernel_0", HEIGHT_13, WIDTH_13, HEIGHT_14, WIDTH_14, IP_FM_13, IP_FM_14, 1);

	//Layer 14 Point-Wise Convolution

	layer_count++;
	float* op_fm_14 = (float*) malloc(IP_FM_15 * HEIGHT_15 * WIDTH_15 * sizeof(float));	//output feature map for layer 14
	convPointwise(op_fm_13, op_fm_14, "gamma/conv_pw_7_bn_gamma_0", "beta/conv_pw_7_bn_beta_0", "mean/conv_pw_7_bn_moving_mean_0", "variance/conv_pw_7_bn_moving_variance_0", "weights_float/conv_pw_7_kernel_0", HEIGHT_14, WIDTH_14, HEIGHT_15, WIDTH_15, IP_FM_14, IP_FM_15);

	//Layer 15 Depth-Wise Convolution

	layer_count++;
	float* op_fm_15 = (float*) malloc(IP_FM_16 * HEIGHT_16 * WIDTH_16 * sizeof(float)); //output feature map for layer 15
	convDepthwise(op_fm_14, op_fm_15, "gamma/conv_dw_8_bn_gamma_0", "beta/conv_dw_8_bn_beta_0", "mean/conv_dw_8_bn_moving_mean_0", "variance/conv_dw_8_bn_moving_variance_0", "weights_float/conv_dw_8_depthwise_kernel_0", HEIGHT_15, WIDTH_15, HEIGHT_16, WIDTH_16, IP_FM_15, IP_FM_16, 1);

	//Layer 16 Point-Wise Convolution

	layer_count++;
	float* op_fm_16 = (float*) malloc(IP_FM_17 * HEIGHT_17 * WIDTH_17 * sizeof(float));	//output feature map for layer 16
	convPointwise(op_fm_15, op_fm_16, "gamma/conv_pw_8_bn_gamma_0", "beta/conv_pw_8_bn_beta_0", "mean/conv_pw_8_bn_moving_mean_0", "variance/conv_pw_8_bn_moving_variance_0", "weights_float/conv_pw_8_kernel_0", HEIGHT_16, WIDTH_16, HEIGHT_17, WIDTH_17, IP_FM_16, IP_FM_17);

	//Layer 17 Depth-Wise Convolution

	layer_count++;
	float* op_fm_17 = (float*) malloc(IP_FM_18 * HEIGHT_18 * WIDTH_18 * sizeof(float)); //output feature map for layer 17
	convDepthwise(op_fm_16, op_fm_17, "gamma/conv_dw_9_bn_gamma_0", "beta/conv_dw_9_bn_beta_0", "mean/conv_dw_9_bn_moving_mean_0", "variance/conv_dw_9_bn_moving_variance_0", "weights_float/conv_dw_9_depthwise_kernel_0", HEIGHT_17, WIDTH_17, HEIGHT_18, WIDTH_18, IP_FM_17, IP_FM_18, 1);

	//Layer 18 Point-Wise Convolution

	layer_count++;
	float* op_fm_18 = (float*) malloc(IP_FM_19 * HEIGHT_19 * WIDTH_19 * sizeof(float));	//output feature map for layer 18
	convPointwise(op_fm_17, op_fm_18, "gamma/conv_pw_9_bn_gamma_0", "beta/conv_pw_9_bn_beta_0", "mean/conv_pw_9_bn_moving_mean_0", "variance/conv_pw_9_bn_moving_variance_0", "weights_float/conv_pw_9_kernel_0", HEIGHT_18, WIDTH_18, HEIGHT_19, WIDTH_19, IP_FM_18, IP_FM_19);

	//Layer 19 Depth-Wise Convolution

	layer_count++;
	float* op_fm_19 = (float*) malloc(IP_FM_20 * HEIGHT_20 * WIDTH_20 * sizeof(float)); //output feature map for layer 19
	convDepthwise(op_fm_18, op_fm_19, "gamma/conv_dw_10_bn_gamma_0", "beta/conv_dw_10_bn_beta_0", "mean/conv_dw_10_bn_moving_mean_0", "variance/conv_dw_10_bn_moving_variance_0", "weights_float/conv_dw_10_depthwise_kernel_0", HEIGHT_19, WIDTH_19, HEIGHT_20, WIDTH_20, IP_FM_19, IP_FM_20, 1);

	//Layer 20 Point-Wise Convolution

	layer_count++;
	float* op_fm_20 = (float*) malloc(IP_FM_21 * HEIGHT_21 * WIDTH_21 * sizeof(float));	//output feature map for layer 20
	convPointwise(op_fm_19, op_fm_20, "gamma/conv_pw_10_bn_gamma_0", "beta/conv_pw_10_bn_beta_0", "mean/conv_pw_10_bn_moving_mean_0", "variance/conv_pw_10_bn_moving_variance_0", "weights_float/conv_pw_10_kernel_0", HEIGHT_20, WIDTH_20, HEIGHT_21, WIDTH_21, IP_FM_20, IP_FM_21);

	//Layer 21 Depth-Wise Convolution

	layer_count++;
	float* op_fm_21 = (float*) malloc(IP_FM_22 * HEIGHT_22 * WIDTH_22 * sizeof(float)); //output feature map for layer 21
	convDepthwise(op_fm_20, op_fm_21, "gamma/conv_dw_11_bn_gamma_0", "beta/conv_dw_11_bn_beta_0", "mean/conv_dw_11_bn_moving_mean_0", "variance/conv_dw_11_bn_moving_variance_0", "weights_float/conv_dw_11_depthwise_kernel_0", HEIGHT_21, WIDTH_21, HEIGHT_22, WIDTH_22, IP_FM_21, IP_FM_22, 1);

	//Layer 22 Point-Wise Convolution

	layer_count++;
	float* op_fm_22 = (float*) malloc(IP_FM_23 * HEIGHT_23 * WIDTH_23 * sizeof(float));	//output feature map for layer 22
	convPointwise(op_fm_21, op_fm_22, "gamma/conv_pw_11_bn_gamma_0", "beta/conv_pw_11_bn_beta_0", "mean/conv_pw_11_bn_moving_mean_0", "variance/conv_pw_11_bn_moving_variance_0", "weights_float/conv_pw_11_kernel_0", HEIGHT_22, WIDTH_22, HEIGHT_23, WIDTH_23, IP_FM_22, IP_FM_23);

	//Layer 23 Depth-Wise Convolution

	layer_count++;
	float* op_fm_23 = (float*) malloc(IP_FM_24 * HEIGHT_24 * WIDTH_24 * sizeof(float)); //output feature map for layer 23
	convDepthwise(op_fm_22, op_fm_23, "gamma/conv_dw_12_bn_gamma_0", "beta/conv_dw_12_bn_beta_0", "mean/conv_dw_12_bn_moving_mean_0", "variance/conv_dw_12_bn_moving_variance_0", "weights_float/conv_dw_12_depthwise_kernel_0", HEIGHT_23, WIDTH_23, HEIGHT_24, WIDTH_24, IP_FM_23, IP_FM_24, 2);

	//Layer 24 Point-Wise Convolution

	layer_count++;
	float* op_fm_24 = (float*) malloc(IP_FM_25 * HEIGHT_25 * WIDTH_25 * sizeof(float));	//output feature map for layer 24
	convPointwise(op_fm_23, op_fm_24, "gamma/conv_pw_12_bn_gamma_0", "beta/conv_pw_12_bn_beta_0", "mean/conv_pw_12_bn_moving_mean_0", "variance/conv_pw_12_bn_moving_variance_0", "weights_float/conv_pw_12_kernel_0", HEIGHT_24, WIDTH_24, HEIGHT_25, WIDTH_25, IP_FM_24, IP_FM_25);

	//Layer 25 Depth-Wise Convolution

	layer_count++;
	float* op_fm_25 = (float*) malloc(IP_FM_26 * HEIGHT_26 * WIDTH_26 * sizeof(float)); //output feature map for layer 25
	convDepthwise(op_fm_24, op_fm_25, "gamma/conv_dw_13_bn_gamma_0", "beta/conv_dw_13_bn_beta_0", "mean/conv_dw_13_bn_moving_mean_0", "variance/conv_dw_13_bn_moving_variance_0", "weights_float/conv_dw_13_depthwise_kernel_0", HEIGHT_25, WIDTH_25, HEIGHT_26, WIDTH_26, IP_FM_25, IP_FM_26, 1);

	//Layer 26 Point-Wise Convolution

	layer_count++;
	float* op_fm_26 = (float*) malloc(IP_FM_27 * HEIGHT_27 * WIDTH_27 * sizeof(float));	//output feature map for layer 26
	convPointwise(op_fm_25, op_fm_26, "gamma/conv_pw_13_bn_gamma_0", "beta/conv_pw_13_bn_beta_0", "mean/conv_pw_13_bn_moving_mean_0", "variance/conv_pw_13_bn_moving_variance_0", "weights_float/conv_pw_13_kernel_0", HEIGHT_26, WIDTH_26, HEIGHT_27, WIDTH_27, IP_FM_26, IP_FM_27);
	
	// for (k = 0; k < IP_FM_27; k++){
	// 	for (j = 0; j < 7; j++){
	// 		for(i = 0; i < 7; i++){
	// 			printf("%f\t", op_fm_26[(j*WIDTH_27+i) + (k*HEIGHT_27*WIDTH_27)]);
	// 		}
	// 		printf("\n");
	// 	}
    // 	printf("\n");
	// } 

	//Layer 27 Average Pool

	layer_count++;
	float* op_fm_27 = (float*) malloc(ELEMENTS * HEIGHT_28 * WIDTH_28 * sizeof(float));	//output feature map for layer 27
	convAvgPool(op_fm_26, op_fm_27, HEIGHT_27, WIDTH_27, HEIGHT_28, WIDTH_28, IP_FM_27, ELEMENTS);
	// for (k = 0; k < ELEMENTS; k++){
	// 	printf("%e\t", op_fm_27[k]);
	// } 
	//Layer 28 Fully COnnected
	printf("Avg pool done\n");
	layer_count++;
	float* op_fm_28 = (float*) malloc(CLASSES_SOFTMAX * HEIGHT_29 * WIDTH_29 * sizeof(float));	//output feature map for layer 28
	fullyConectedLayer(op_fm_27, op_fm_28, "bias/conv_preds_bias_0", "weights_float/conv_preds_kernel_0", CLASSES, ELEMENTS);
	for (k = 0; k < CLASSES; k++){
		printf("%e\t", op_fm_28[k]);
	} 

	//Layer 29 Softmax

	layer_count++;
	softmax(op_fm_28);

	//Shutdown and cleanup
	free(filter);
	/* free(op_fm_0);	free(op_fm_1);	free(op_fm_2);	free(op_fm_3);
	free(op_fm_4);	free(op_fm_5);	free(op_fm_6);	free(op_fm_7);
	free(op_fm_8);	free(op_fm_9);	free(op_fm_10);	free(op_fm_11);
	free(op_fm_12);	free(op_fm_13);	free(op_fm_14);	free(op_fm_15);
	free(op_fm_16);	free(op_fm_17);	free(op_fm_18);	free(op_fm_19);
	free(op_fm_20);	free(op_fm_21);	free(op_fm_22);	free(op_fm_23);
	free(op_fm_24);	free(op_fm_25);	free(op_fm_26);	free(op_fm_27);
	free(op_fm_28); */
	clReleaseMemObject(d_output);
	clReleaseMemObject(d_filter);
	clReleaseProgram(program);
	clReleaseKernel(standard_conv);
	clReleaseKernel(depthwise_conv);
	clReleaseKernel(pointwise_conv);
	clReleaseCommandQueue(commands);
	clReleaseContext(context);
	return 0;
}
