/* Written by Andrey Osenenko, my mail is noko@nm.ru.
 * Do not steal my art. */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

/* Yes, it's platform-dependant. Some files have kana in names
 * and there is no way to write unicode filenames (in windows) without
 * using windows natives. Also 64-bit fseek and mkdir. */
#include <windows.h>

int usage(int argc,char **argv){
	fprintf(stderr,
		"Usage: %s DATA.ROM directory\n"
		"    Extracts contents of ps3 umineko archive into directory\n"
		"Usage: %s file.pic file.bmp\n"
		"    Converts ps3 umineko picture file.pic to windows bitmap file.bmp\n"
		"Usage: %s file.bup file\n"
		"    Creates multiple windows bitmap files named file<emotion name>.bmp\n"
		"    from ps3 umineko character sprite file.bup\n"
		"Usage: %s file.txa file\n"
		"    Creates multiple windows bitmap files named file_<sub-title>.bmp\n"
		"    from ps3 umineko picture collection file.txa\n"
		"",
		argv[0],argv[0],argv[0],argv[0]
	);
	exit(1);
}

int die(int code,char *fmt,...){
	va_list args;
	va_start(args,fmt);

	vfprintf(stderr,fmt,args);

	va_end(args);

	exit(code);
}

#define SCANLINE(w) (4*(((w)+3)&0xfffc))

typedef struct pic_header_t{
	int magic;
	int filesize;
	unsigned short ew;
	unsigned short eh;
	unsigned short w;
	unsigned short h;
	int unk;
	int chunks;
} pic_header;

typedef struct pic_chunk_t{
	int version;
	unsigned short left;
	unsigned short top;
	unsigned short w;
	unsigned short h;
	int off;
	int size;
} pic_chunk;

typedef struct bup_header_t{
	int magic;
	int filesize;
	int unk0;
	unsigned short left;
	unsigned short top;
	unsigned short w;
	unsigned short h;
	int off;
	int size;
	int chunks;
} bup_header;

typedef struct bup_chunk_t{
	char title[16];
	int unk0;

	struct{
		unsigned short left;
		unsigned short top;
		unsigned short w;
		unsigned short h;
		int off;
		int size;
	} pic[2];

	int unka;
	int unkb;
	int unkc;
	int unkd;

} bup_chunk;

typedef struct txa_header_t{
	int magic;
	int filesize;
	int off;
	int encsize;
	int decsize;
	int chunks;

	int unk1;
	int unk2;
} txa_header;

typedef struct txa_chunk_t{
	unsigned short length;
	unsigned short index;
	unsigned short w;
	unsigned short h;
	unsigned short scanline;
	unsigned short unk1;
	unsigned int off;
	char name[0];
} txa_chunk;

typedef struct pck_chunk_t{
	unsigned int nameoff;
	unsigned int off;
	unsigned int size;
} pck_chunk;

unsigned char bmp_header[122] = {
    0x42, 0x4D, 0xDA, 0x11, 0xC6, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x7A, 0x00, 0x00, 0x00, 0x6C, 0x00,
    0x00, 0x00, 0x7D, 0x09, 0x00, 0x00, 0x38, 0x05,
    0x00, 0x00, 0x01, 0x00, 0x20, 0x00, 0x03, 0x00,
    0x00, 0x00, 0x60, 0x11, 0xC6, 0x00, 0x13, 0x0B,
    0x00, 0x00, 0x13, 0x0B, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0x00, 0x00, 0xFF, 0x00, 0x00, 0xFF, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x56, 0xB8, 0x1E, 0xFC, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x66, 0x66,
    0x66, 0xFC, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x63, 0xF8, 0x28, 0xFF, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00 
};

/* It's a compression scheme similar to LZSS but with data itself
 * instead of 4096 byte circular buffer (which makes a lot of sense
 * to me) and different format of backreferences. */
void decode(unsigned char *buffer,int size,unsigned char *result){
	unsigned char *res=result;
	int p=0;
	int marker=1;
	int j;

	while(p<size){
		if(marker==1) marker=0x100|buffer[p++];

		if(marker&1){
			unsigned short v=(buffer[p+0]<<8)|buffer[p+1];
			int count,offset;
			unsigned char *pos;

			if(v&0x8000){
				count=((v>>5)&0x3ff)+3;
				offset=v&0x1f;
			} else{
				count=(v>>11)+3;
				offset=(v&0x7ff)+32;
			}

			pos=res-(offset+1)*4;

			for(j=0;j<count;j++)
				*res++=*pos++;

			p+=2;
		} else{
			*res++=buffer[p++];
		}

		marker>>=1;
	}
}

/* After decompression only the first scanline in pictures is filled bytes
 * representing colors. Remaining scanlines instead have differences from
 * previous scanlines in them. If a pixel at (2,3) is represented by
 * bytes 0x00 0x00 0x00 0x00, then it means that its color is the same as
 * color of pixel at (2,2). This fuctions fixes this and makes all bytes
 * represent colors instead of differences.
 *
 * This function is also horribly mislabeled. */
void dpcm(unsigned char *src,unsigned char *dst,int w,int h,int scanline){
	int x;
	
	for(x=scanline;x<scanline*h;x++){
		dst[x]+=src[x-scanline];
	}
}

void blit(unsigned char *src,int w,int h,int scanline,unsigned char *dst,int dx,int dy,int dstscanline){
	int x;
	int y;
	
	for(y=0;y<h;y++){
		for(x=0;x<w*4;x++){
			dst[dx*4+x+(dy+y)*dstscanline]=src[x+y*scanline];
		}
	}
}

/* Not a proper over blend but instead "copy-unless-translaprent" */
void blend(unsigned char *src,int w,int h,int scanline,unsigned char *dst,int dx,int dy,int dstscanline){
	int x;
	int y;
	int i;

	for(y=0;y<h;y++){
		for(x=0;x<w;x++){
			int d=(dx+x)*4+(dy+y)*dstscanline;
			int s=x*4+y*scanline;
			
			int da=dst[d+3];
			int sa=src[s+3];

			if(sa!=0) 
			for(i=0;i<4;i++)
				dst[d+i]=src[s+i];

		}
	}
}

/* Ten years ago, I would have killed for such a simple way to write pictures
 * programmatically... */
void write_bmp(char *filename,unsigned char *data,int w,int h,int scanline){
	FILE *out;
	int i,j;

	if((out=fopen(filename,"wb"))==NULL)
		die(4,"Couldn't open file %s for writing.\n",filename);

	*(unsigned long *)(bmp_header+2)=122+w*h*4;
	*(unsigned long *)(bmp_header+18)=w;
	*(unsigned long *)(bmp_header+22)=h;
	*(unsigned long *)(bmp_header+34)=w*h*4;

	fwrite(bmp_header,sizeof(bmp_header),1,out);

	for(j=h-1;j>=0;j--){
		for(i=0;i<w;i++){
			fwrite(&data[i*4+j*scanline],4,1,out);
		}
	}

	fclose(out);
}

int process_pic(FILE *in,char *output){
	pic_header header;
	pic_chunk *chunks;
	unsigned char *data;
	int i;

	fread(&header,sizeof(header),1,in);

	chunks=calloc(header.chunks,sizeof(pic_chunk));
	fread(chunks,sizeof(pic_chunk),header.chunks,in);

	data=calloc(1,4*header.w*header.h);
	
	for(i=0;i<header.chunks;i++){
		pic_chunk *chunk=chunks+i;

		int size=chunk->size;
		unsigned char *buffer=malloc(size);
		unsigned char *result=malloc(chunk->h*SCANLINE(chunk->w));

		fseek(in,chunk->off,0);
		fread(buffer,size,1,in);

		decode(buffer,size,result);
		dpcm(result,result,chunk->w,chunk->h,SCANLINE(chunk->w));
		blit(result,chunk->w,chunk->h,SCANLINE(chunk->w),data,chunk->left,chunk->top,header.w*4);

		free(buffer);
		free(result);
	}

	write_bmp(output,data,header.w,header.h,header.w*4);

	return 0;
}

int process_bup(FILE *in,char *output){
	bup_header header;
	bup_chunk *chunks;
	unsigned char *data;
	unsigned char *buffer;
	int i;
	char str[0x100];
	char meta_info_filename[0x100];

	fread(&header,sizeof(header),1,in);

	chunks=calloc(header.chunks,sizeof(bup_chunk));
	fread(chunks,sizeof(bup_chunk),header.chunks,in);

	data=calloc(1,SCANLINE(header.w)*header.h*4);
	
	buffer=malloc(header.size);
	fseek(in,header.off,0);
	fread(buffer,header.size,1,in);

	sprintf(meta_info_filename, "%s.txt", output);
	FILE * f_meta_info = fopen(meta_info_filename, "w");
	if (f_meta_info == NULL)
	{
		printf("Error opening meta info output file!\n");
		exit(1);
	}
	fprintf(f_meta_info, "%d, %d\n", header.left, header.top);
	fclose(f_meta_info);

	sprintf(str,"%s.bmp",output);
	decode(buffer,header.size,data);
	dpcm(data,data,header.w,header.h,SCANLINE(header.w));
	write_bmp(str,data,header.w,header.h,SCANLINE(header.w));

	//The very first ouput image (the first chunk) was output without the filename.
	//This code ensures the first image has the correct filename.
	wchar_t wname[0x100] = { 0, };
	wchar_t wrealname[0x100] = { 0, };

	//convert already written image filename to wide char
	MultiByteToWideChar(932, 0, str, sizeof(str), wname, 0x100);

	sprintf(str, "%s%s.bmp", output, chunks[0].title);

	//create the final image filename
	MultiByteToWideChar(932, 0, str, sizeof(str), wrealname, 0x100);

	//rename file (overwrite if already exists by deleting first)
	DeleteFileW(wrealname);
	MoveFileW(wname, wrealname);

	free(buffer);

	for(i=0;i<header.chunks;i++){
		bup_chunk *chunk=chunks+i;
		char name[0x100];
		wchar_t wname[0x100]={0,};
		wchar_t wrealname[0x100]={0,};
		unsigned char *xdata,*redata;

		if(chunk->pic[0].w==0)
			continue;

		xdata=malloc(SCANLINE(chunk->pic[0].w)*chunk->pic[0].h);
		buffer=malloc(chunk->pic[0].size);
		
		fseek(in,chunk->pic[0].off,0);
		fread(buffer,chunk->pic[0].size,1,in);

		decode(buffer,chunk->pic[0].size,xdata);
		dpcm(xdata,xdata,chunk->pic[0].w,chunk->pic[0].h,SCANLINE(chunk->pic[0].w));

		redata=malloc(SCANLINE(header.w)*header.h);
		memcpy(redata,data,SCANLINE(header.w)*header.h);
		blend(xdata,chunk->pic[0].w,chunk->pic[0].h,SCANLINE(chunk->pic[0].w),redata,chunk->pic[0].left,chunk->pic[0].top,SCANLINE(header.w));

		sprintf(name,"%s%d.bmp",output,i);
		write_bmp(name,redata,header.w,header.h,SCANLINE(header.w));

		free(buffer);
		free(xdata);

		sprintf(str,"%s%s.bmp",output,chunk->title);

		/* And here goes the platform-independance. Of course character encodings
		 * are always a touchy suject. On linux you'd probably have to use iconv
		 * and convert file names to utf8 instead. */

		MultiByteToWideChar(932,0,name,sizeof(name),wname,0x100);
		MultiByteToWideChar(932,0,str,sizeof(str),wrealname,0x100);

		DeleteFileW(wrealname);
		MoveFileW(wname,wrealname);
	}

	free(data);

	return 0;
}

int process_txa(FILE *in,char *output){
	txa_header header;
	txa_chunk **chunks;
	unsigned char *metadata,*p;
	unsigned char *data;
	unsigned char *buffer;
	int i;
	char str[0x100];

	fread(&header,sizeof(header),1,in);

	metadata=malloc(header.off-sizeof(header));
	chunks=malloc(header.chunks*sizeof(txa_chunk **));
	fread(metadata,header.off-sizeof(header),1,in);

	for(p=metadata,i=0;i<header.chunks;i++){
		chunks[i]=(txa_chunk *)p;
		p+=chunks[i]->length;
	}

	data=malloc(header.decsize);
	buffer=malloc(header.encsize);
	fseek(in,header.off,0);
	fread(buffer,header.encsize,1,in);
	decode(buffer,header.encsize,data);
	free(buffer);

	for(i=0;i<header.chunks;i++){
		sprintf(str,"%s_%s.bmp",output,chunks[i]->name);
		write_bmp(str,data+chunks[i]->off,chunks[i]->w,chunks[i]->h,chunks[i]->scanline);
	}

	free(data);
	return 0;
}

static void unpack(FILE *handle,__int64 offset,char *directory){
	pck_chunk *chunks;
	unsigned int count,i;

	_fseeki64(handle,offset,0);
	fread(&count,sizeof(count),1,handle);

	chunks=malloc(count*sizeof(pck_chunk));
	fread(chunks,sizeof(pck_chunk),count,handle);

	for(i=0;i<count;i++){
		char s[0x400],name[0x100];
		int isdir;
		__int64 off;
		int size;
		
		pck_chunk *chunk=&chunks[i];
		size=chunk->size;
		off=chunk->off;

		isdir=chunk->nameoff&0x80000000;
		chunk->nameoff&=~0x80000000;
		off<<=isdir?4:11;

		fseek(handle,offset+chunk->nameoff,0);
		fread(name,1,0x100,handle);

		if(name[0]=='.' && name[1]=='\0' || name[0]=='.' && name[1]=='.' && name[2]=='\0')
			continue;

		sprintf(s,"%s/%s",directory,name);

		if(isdir){
			CreateDirectoryA(s,NULL);
			unpack(handle,off,s);
		} else{
			FILE *f;

			if((f=fopen(s,"wb"))==NULL)
				die(4,"Couldn't open file %s for writing.\n",s);

			_fseeki64(handle,off,0);

			while(size>0){
				char buffer[0x400];
				int c=size>0x400?0x400:size;

				fread(buffer,c,1,handle);
				fwrite(buffer,c,1,f);

				size-=c;
			}

			fclose(f);
		}
	}
}

int process_pck(FILE *in,char *output){
	CreateDirectoryA(output,NULL);
	unpack(in,0x10,output);

	return 0;
}

int main(int argc,char **argv){
	FILE *in;
	int magic;

	if(argc!=3) usage(argc,argv);

	if((in=fopen(argv[1],"rb"))==NULL)
		die(2,"Couldn't open file %s for reading.\n",argv[1]);

	fread(&magic,4,1,in);
	fseek(in,0,0);

	switch(magic){
	case 0x33434950: return process_pic(in,argv[2]);
	case 0x33505542: return process_bup(in,argv[2]);
	case 0x33415854: return process_txa(in,argv[2]);
	case 0x204d4f52: return process_pck(in,argv[2]);
	default: die(3,"%s: unknown magic: %08x\n",argv[1],magic);
	}
}

