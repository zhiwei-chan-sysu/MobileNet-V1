
__kernel void convolute(__global float* output, 
						__global float* inp_image_r, 
						__global float* inp_image_g, 
						__global float* inp_image_b, 
						__global float* filter_k,
						int rows, int cols, int filtersize, int stride, int op_size ) {

	int tx = get_global_id(0);
	int ty = get_global_id(1);
	//int half_filtersize = (filtersize)/2;

	float sum = 0;
	int xindex=0, yindex=0, findex=0, filter_count=0;
	int i,j,l;
	while (filter_count < op_size) {
	
		int output_shift = (rows / 2) * (cols / 2) * filter_count;
		
		for(i = 0; i < filtersize; i++){
			yindex = ty * stride + i;
			for(j = 0; j < filtersize; j++,findex++){
				xindex = tx * stride + j;
				if (yindex >= cols || xindex >= rows) {
					sum +=  0 * filter_k[findex];
				}
				else {
					// if ((tx == 111 && ty == 111) && filter_count == 0) {
					// 	printf("Img r: %f\tfilter index %d\t%e\n",inp_image_r[yindex * get_global_size(0) * stride + xindex], findex, filter_k[findex]);
					// 	printf("Multiplication of R: %e\n",inp_image_r[yindex * get_global_size(0) * stride + xindex] * filter_k[findex]);
					// }
 					sum +=  inp_image_r[yindex * get_global_size(0) * stride + xindex] * filter_k[findex];
				}
			}
		}
		// if (tx == 0 && ty == 0) 
		//  	printf("Sum of R: %f\n", sum);
		for(i = 0; i < filtersize; i++){
			yindex = ty * stride + i;
			for(j = 0; j < filtersize; j++,findex++){
				xindex = tx * stride + j;
				if (yindex >= cols || xindex >= rows) {
					sum +=  0 * filter_k[findex];
				}
				else {
					// if ((tx == 111 && ty == 111) && filter_count == 0) {
					// 	printf("Img g: %f\tfilter index %d\t%e\n",inp_image_g[yindex * get_global_size(0) * stride + xindex], findex, filter_k[findex]);
					// 	printf("Multiplication of G: %e\n",inp_image_g[yindex * get_global_size(0) * stride + xindex] * filter_k[findex]);
					// }
 					sum +=  inp_image_g[yindex * get_global_size(0) * stride + xindex] * filter_k[findex];
				}
			}
		}
		// if (tx == 0 && ty == 0) 
		//  	printf("Sum of G: %f\n", sum);
		for(i = 0; i < filtersize; i++){
			yindex = ty * stride + i;
			for(j = 0; j < filtersize; j++,findex++){
				xindex = tx * stride + j;
				if (yindex >= cols || xindex >= rows) {
					sum +=  0 * filter_k[findex];
				}
				else {
					// if ((tx == 111 && ty == 111) && filter_count == 0) {
					// 	printf("Img b: %f\tfilter index %d\t%e\n",inp_image_b[yindex * get_global_size(0) * stride + xindex], findex, filter_k[findex]);
					// 	printf("Multiplication of B: %e\n",inp_image_b[yindex * get_global_size(0) * stride + xindex] * filter_k[findex]);
					// }
 					sum +=  inp_image_b[yindex * get_global_size(0) * stride + xindex] * filter_k[findex];

				}
			}
		}
		
		
		// if (tx == 0 && ty == 0) {
		// 	//printf("M: %f\tbias: %f\t\n",M,Sbias);
		// 	//printf("Summ: %d\t\n",(int)((M * sum) + (bias[filter_count] * Sbias)));
		// 	printf("Sum: %f\t\n",sum);
		// }
		
		output[(ty * get_global_size(0) + tx) + output_shift] = sum;
		// if ((tx == 111 && ty == 111) && filter_count == 0) {
		// 	//printf("M: %f\tbias: %f\t\n",M,Sbias);
		// 	//printf("Summ: %d\t\n",(int)((M * sum) + (bias[filter_count] * Sbias)));
		// 	printf("Sum: %e\t\n",sum);
		// }
		sum = 0;
		filter_count++;
	}
}

__kernel void depthwise(__global float* output, 
						__global float* inp_image, 
						__global float* filter_k, 
						int rows, int cols, int filtersize, int stride, int op_size ) { 

	int tx = get_global_id(0);
	int ty = get_global_id(1);
	int start, end;
	int half_filtersize = (filtersize)/2;

	if (stride == 1) {
		start = -half_filtersize;
		end = half_filtersize;
	} else if (stride == 2) {
		start = 0;
		end = filtersize - 1;
	}
	float sum = 0;
	int xindex=0, yindex=0, findex=0, filter_count=0;
	int i,j,l;
	while (filter_count < op_size) {
		int output_shift = (rows / stride) * (cols / stride) * filter_count;
		
		for(i = start; i <= end; i++){
			yindex = ty * stride + i;
			for(j = start; j <= end; j++,findex++){
				xindex = tx * stride + j;
				
				if (stride == 1) {
					if ((yindex < 0 || xindex < 0) || (yindex >= cols || xindex >= rows)) {
						sum +=  0 * filter_k[findex];
					}
					else {
						// if ((tx == 111 && ty == 111) && filter_count == 1) {
						// 	printf("Img data: %e\tfilter index %d\t%e\n",inp_image[(yindex * get_global_size(0) * stride + xindex) + (rows * cols * filter_count)], findex, filter_k[findex]);
						// 	printf("Multiplication: %e\n",inp_image[yindex * get_global_size(0) * stride + xindex] * filter_k[findex]);
						// }
						sum +=  inp_image[(yindex * get_global_size(0) * stride + xindex) + (rows * cols * filter_count)] * filter_k[findex];
					}
				} else if (stride == 2) {
					if (yindex >= cols || xindex >= rows) {
						sum +=  0 * filter_k[findex];
					}
					else {
						// if ((tx == 111 && ty == 111) && filter_count == 1) {
						// 	printf("Img data: %e\tfilter index %d\t%e\n",inp_image[(yindex * get_global_size(0) * stride + xindex) + (rows * cols * filter_count)], findex, filter_k[findex]);
						// 	printf("Multiplication: %e\n",inp_image[yindex * get_global_size(0) * stride + xindex] * filter_k[findex]);
						// }
						sum +=  inp_image[(yindex * get_global_size(0) * stride + xindex) + (rows * cols * filter_count)] * filter_k[findex];
					}
				}
			}
		}

		//sum = sum + bias[filter_count];
		
		// if ((tx == 111 && ty == 111) && filter_count == 1) {
		// 	//printf("M: %f\tbias: %f\t\n",M,Sbias);
		// 	//printf("Summ: %d\t\n",(int)((M * sum) + (bias[filter_count] * Sbias)));
		// 	printf("Depth Sum: %e\t\n",sum);
		// }


		output[(ty * get_global_size(0) + tx) + output_shift] = sum;
		sum = 0;
		filter_count++;	
	}
}

__kernel void pointwise(__global float* output, 
						__global float* inp_image, 
						__global float* filter_k, 
						int rows, int cols, int filtersize, int op_size ) {  

	int tx = get_global_id(0);
	int ty = get_global_id(1);

	float sum = 0;
	int findex=0, filter_count=0;
	int i,j,l;
	while (filter_count < op_size) {
		int output_shift = rows * cols * filter_count;
		
		for (i = 0; i < filtersize; i++, findex++) {
			sum += inp_image[(ty * get_global_size(0) + tx) + (rows * cols * i)] * filter_k[findex]; 
		}

		// if ((tx == 0 && ty == 0) && filter_count == 0) {
		// 	printf("Img data: %f\tfilter index %d\t%f\n",inp_image[(ty * get_global_size(0) + tx) + (rows * cols * i)], findex, filter_k[findex]);
		// 	printf("Multiplication: %f\n",inp_image[(ty * get_global_size(0) + tx) + (rows * cols * i)] * filter_k[findex]);
		// }

		output[(ty * get_global_size(0) + tx) + output_shift] = sum;
		sum = 0;
		filter_count++;
	}
}
__kernel void avgPool(__global float* output, 
					  __global float* inp_image, 
					  int rows, int cols ) {

        int tx = get_global_id(0);
        float sum = 0;
        int i;
	    int input_shift = rows * cols;
		for (i = 0; i < rows * cols; i++) {
			sum += inp_image[i + ( tx * input_shift)];
		}
		//printf("M: %f\tbias: %f\t\n",M,Sbias);
		//printf("Summ: %d\t\n",(int)((M * sum) + (bias[filter_count] * Sbias)));
		//printf("Sum/49: %d\t\n",sum/49);

		output[tx] = sum / 49;
}