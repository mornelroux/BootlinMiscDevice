// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>

//M. Le Roux include
#include <linux/of_device.h>            //of functions
#include <linux/io.h>                     //readl, writel functions
#include <linux/serial_reg.h>           //Standardized UART values
#include <linux/pm_runtime.h>           //Power management functions
#include <linux/miscdevice.h>           //
#include <linux/fs.h>                   //Needed for file operation struct

//Private structure to keep pointers between logical & physical devices
struct serial_dev {
        void __iomem *regs;
        struct miscdevice miscdev;
};


//M Le Roux
static int serial_read(struct file *, char __user *, size_t, loff_t *)
{
        return 0;
}

static int serial_write(struct file *, const char __user *, size_t, loff_t *)
{
        return 0;
}

static const struct file_operations serial_fops = {
        .owner = THIS_MODULE,
        .read = serial_read,
        .write = serial_write,
};


static u32 reg_read(struct serial_dev *serial, unsigned int reg){
        return readl(serial->regs + reg*4);
}

static void reg_write(struct serial_dev *serial, u32 val, uint reg){
        writel(val, (serial->regs + reg*4));
}

static void write_uart_char(struct serial_dev *serial, char *p){
        while(!(reg_read(serial, UART_LSR) && UART_LSR_THRE)) {
                cpu_relax();
        }
        reg_write(serial, *p, UART_TX);
}


static int serial_probe(struct platform_device *pdev)
{
        //M.Le Roux
        //Allocate physical memory for the private device structure
        struct serial_dev *serial;
        uint uartclk, baud_divisor;
        int resp;

        //Kernel logical memory space is mapped to the physical addresses of the UART registers.
        serial = devm_kzalloc(&pdev->dev,sizeof(*serial), GFP_KERNEL);
        if (!serial){
                return -ENOMEM; //Error 'No Memory'
        };

        //Get base virtual address for device registers
        serial->regs = devm_platform_ioremap_resource(pdev,0);
        if(IS_ERR(serial->regs)){
                return PTR_ERR(serial->regs);
        };

        //Power management initialization
        pm_runtime_enable(&pdev->dev);
        pm_runtime_get_sync(&pdev->dev);

        //Get the clock frequency
        resp = of_property_read_u32(pdev->dev.of_node, "clock-frequency", &uartclk);
        if (resp){
                dev_err(&pdev->dev, "Clock-frequency property not found.\n");
                return resp;
        }

        //Calculate baud divisor
        baud_divisor = uartclk/16/115200;
        
        //Setup UART registers
        reg_write(serial, 0x07, UART_OMAP_MDR1);                //Disable UART mode, disables controller
        reg_write(serial, 0x00, UART_LCR);                      //Clears the LCR register for Register Access Control
        reg_write(serial, UART_LCR_CONF_MODE_A, UART_LCR);      //Sets registers access control to mode A, (same as using UART_LCR_DLAB)
        reg_write(serial, baud_divisor & 0xff, UART_DLL);       //Writes the LOWER 8bits of the baud divisor to DLL
        reg_write(serial, (baud_divisor >> 8) & 0xff, UART_DLM);//Writes the HIGHER 8bits of the baud divisor to DLM
        reg_write(serial, UART_LCR_WLEN8, UART_LCR);            //Byte size setup
        reg_write(serial, 0x00, UART_OMAP_MDR1);                //Starts the UART controller

        //Reset FIFOs
        reg_write(serial, UART_FCR_CLEAR_RCVR | UART_FCR_CLEAR_XMIT, UART_FCR);

        //Write a single character
        write_uart_char(serial, (char*)"M");

        //##   Misc driver registeration   ##//
        //Setup link between MISC device and struct device inside platform device.
        serial->miscdev.this_device = &pdev->dev;
        //Setup link between struct device inside platform device and serial struct
        platform_set_drvdata(pdev,serial);
        //Assign dynamic device name
        struct resource *res;
        res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
        serial->miscdev.name = devm_kasprintf(&pdev->dev, GFP_KERNEL, "serial-%x", res->start);
        //Assign misc device properties
        serial->miscdev.minor = MISC_DYNAMIC_MINOR;
        serial->miscdev.fops = &serial_fops;
        serial->miscdev.parent = &pdev->dev;

        resp = misc_register(&serial->miscdev);
        if (resp){
                pr_err("Misc regsiter failed\n");
                return resp;
        }

	pr_info("MISC driver loaded\n");


	return 0;
}

static int serial_remove(struct platform_device *pdev)
{
        //Power management exit function
        pm_runtime_disable(&pdev->dev);
        struct serial_dev *serial = platform_get_drvdata(pdev);
        misc_deregister(&serial->miscdev);
	pr_info("Called %s\n", __func__);
        return 0;
}

//M. Le Roux
static const struct of_device_id serial_dt_match[] = {
        { .compatible = "bootlin,serial",  },
        { }
};
MODULE_DEVICE_TABLE(of,serial_dt_match);
//

static struct platform_driver serial_driver = {
        .driver = {
                .name = "serial",
                .owner = THIS_MODULE,
                .of_match_table = serial_dt_match,
        },
        .probe = serial_probe,
        .remove = serial_remove,
};

module_platform_driver(serial_driver);

MODULE_LICENSE("GPL");
