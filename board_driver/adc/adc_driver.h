#ifndef ADC_DRIVER_H
#define ADC_DRIVER_H

//#define S5P4418_ADC

#define SET_CHANNEL _IO('A', 0)

/* ADC CHANEL SELECT */
#ifdef CONFIG_FS6818M4
#define ALCOHOL_CHANNEL 7
#define LIGHT_CHANNEL  	6
#define SMOKE_CHANNEL  	5
#define FLAME_CHANNEL 	6
#endif
#ifdef CONFIG_FS6818B
#define LIGHT_CHANNEL 1    //光强传感器
#define SMOKE_CHANNEL 5    //烟雾传感器
#endif
#define POTENTIOMETER 0    //电位器

/*Register Offset*/
#define CON				0x00
#define DAT				0x04
#define INTENB			0x08
#define CLRINT			0x0C
#define PRESCALERCON	0x10

#ifdef S5P4418_ADC /* ARCH = S5P4418 */
#define APEN_BITP		(14)  /* Register is ADCCON */
#define APSV_BITP		(6)
#define ASEL_BITP		(3)
#define ADCON_STBY		(2)
#define ADEN_BITP		(0)
#define AIEN_BITP		(0)
#define AICL_BITP		(0)

#else  /* ARCH = S5P6818 */
/* Register is PRESCALERCON */
#define APEN_BITP		(15)  
#define PRES_BITP		(0)

/* ADCCON */
#define DATA_SEL_VAL	(0)  	/* 0:5clk, 1:4clk, 2:3clk, 3:2clk, 4:1clk, 5:0clk */
#define CLK_CNT_VAL		(6)	 	/* 28nm ADC */

#define DATA_SEL_BITP	(10)	/* 13:10 */
#define CLK_CNT_BITP	(6)		/* 9:6 */
#define ASEL_BITP		(3)
#define ADCON_STBY		(2)
#define ADEN_BITP		(0)

/* ADCINTENB */
#define AIEN_BITP		(0)

/* ADCINTCLR */
#define AICL_BITP		(0)
#endif


#endif
