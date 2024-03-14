/*
 * s2mpb02-core.c - mfd core driver for the Samsung s2mpb02
 *
 * Copyright (c) 2017 Samsung Electronics Co., Ltd
 *              http://www.samsung.com
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 * This driver is based on max77804.c
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/mfd/core.h>
#include <linux/mfd/s2mpb02.h>
#include <linux/mfd/s2mpb02-private.h>
#include <linux/regulator/machine.h>
#include <linux/debugfs.h>

#ifdef	CONFIG_OF
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#endif /* CONFIG_OF */

static struct mfd_cell s2mpb02_devs[] = {
#ifdef	CONFIG_LEDS_S2MPB02
	{ .name = "s2mpb02-led", },
#endif /* CONFIG_LEDS_S2MPB02 */
	{ .name = "s2mpb02-regulator", },
};

int s2mpb02_read_reg(struct i2c_client *i2c, u8 reg, u8 *dest)
{
	struct s2mpb02_dev *s2mpb02 = i2c_get_clientdata(i2c);
	int ret;

	mutex_lock(&s2mpb02->i2c_lock);
	ret = i2c_smbus_read_byte_data(i2c, reg);
	mutex_unlock(&s2mpb02->i2c_lock);
	if (ret < 0) {
		pr_info("%s:%s reg(0x%x), ret(%d)\n",
				MFD_DEV_NAME, __func__, reg, ret);
		return ret;
	}

	ret &= 0xff;
	*dest = ret;
	return 0;
}
EXPORT_SYMBOL_GPL(s2mpb02_read_reg);

int s2mpb02_bulk_read(struct i2c_client *i2c, u8 reg, int count, u8 *buf)
{
	struct s2mpb02_dev *s2mpb02 = i2c_get_clientdata(i2c);
	int ret;

	mutex_lock(&s2mpb02->i2c_lock);
	ret = i2c_smbus_read_i2c_block_data(i2c, reg, count, buf);
	mutex_unlock(&s2mpb02->i2c_lock);
	if (ret < 0)
		return ret;

	return 0;
}
EXPORT_SYMBOL_GPL(s2mpb02_bulk_read);

int s2mpb02_write_reg(struct i2c_client *i2c, u8 reg, u8 value)
{
	struct s2mpb02_dev *s2mpb02 = i2c_get_clientdata(i2c);
	int ret;

	mutex_lock(&s2mpb02->i2c_lock);
	ret = i2c_smbus_write_byte_data(i2c, reg, value);
	mutex_unlock(&s2mpb02->i2c_lock);
	if (ret < 0)
		pr_info("%s:%s reg(0x%x), ret(%d)\n",
				MFD_DEV_NAME, __func__, reg, ret);

	return ret;
}
EXPORT_SYMBOL_GPL(s2mpb02_write_reg);

int s2mpb02_bulk_write(struct i2c_client *i2c, u8 reg, int count, u8 *buf)
{
	struct s2mpb02_dev *s2mpb02 = i2c_get_clientdata(i2c);
	int ret;

	mutex_lock(&s2mpb02->i2c_lock);
	ret = i2c_smbus_write_i2c_block_data(i2c, reg, count, buf);
	mutex_unlock(&s2mpb02->i2c_lock);
	if (ret < 0)
		return ret;

	return 0;
}
EXPORT_SYMBOL_GPL(s2mpb02_bulk_write);

int s2mpb02_update_reg(struct i2c_client *i2c, u8 reg, u8 val, u8 mask)
{
	struct s2mpb02_dev *s2mpb02 = i2c_get_clientdata(i2c);
	int ret, ret_val;
	u8 old_val = 0, new_val = 0;

	mutex_lock(&s2mpb02->i2c_lock);
	ret = i2c_smbus_read_byte_data(i2c, reg);
	if (ret >= 0) {
		old_val = ret & 0xff;
		new_val = (val & mask) | (old_val & (~mask));

		ret = i2c_smbus_write_byte_data(i2c, reg, new_val);
	}

	ret_val = i2c_smbus_read_byte_data(i2c, reg);
	if (ret_val >= 0)
		new_val = ret_val & 0xff;
	mutex_unlock(&s2mpb02->i2c_lock);

	pr_info("s2mpb02(0x%02X): 0x%02X -> 0x%02X\n", reg, old_val, new_val);
	return ret;
}
EXPORT_SYMBOL_GPL(s2mpb02_update_reg);

#ifdef CONFIG_DEBUG_FS
static int s2mpb02_regdump_show(struct seq_file *s, void *unused)
{
	struct s2mpb02_dev *s2mpb02_dev = s->private;
	u8 i, val = 0;

	for (i = S2MPB02_REG_ID; i <= S2MPB02_REG_LDO_DSCH3; i++) {
		s2mpb02_read_reg(s2mpb02_dev->i2c, i, &val);
		seq_printf(s, "0x%x: 0x%x\n", i, val);
	}

	return 0;
}

static int s2mpb02_regdump_open(struct inode *inode, struct file *file)
{
	return single_open(file, s2mpb02_regdump_show, inode->i_private);
}

static const struct file_operations s2mpb02_regdump_operations = {
	.open           = s2mpb02_regdump_open,
	.read           = seq_read,
	.llseek         = seq_lseek,
	.release        = single_release,
};

static void s2mpb02_debugfs_init(struct s2mpb02_dev *s2mpb02)
{
	debugfs_create_file("s2mpb02_regdump", 0440,
			NULL, s2mpb02, &s2mpb02_regdump_operations);
}
#endif

#ifdef	CONFIG_OF
static int of_s2mpb02_dt(struct device *dev,
		struct s2mpb02_platform_data *pdata)
{
	struct device_node *np_s2mpb02 = dev->of_node;
	int count = 0;
	int i, ret;

	if (!np_s2mpb02)
		return -EINVAL;

	pdata->wakeup = of_property_read_bool(np_s2mpb02, "s2mpb02,wakeup");

	count = of_property_count_strings(np_s2mpb02, "s2mpb02,mfd-cell");

	if (!count || (count == -EINVAL)) {
		pdata->devs_num = ARRAY_SIZE(s2mpb02_devs);
		pdata->devs = s2mpb02_devs;

		for (i = 0; i < pdata->devs_num; i++)
			pr_info("%s mfd-cell(%d) = %s\n", __func__, i,
				pdata->devs[i].name);

	} else {
		pdata->devs_num = count;
		pdata->devs = kcalloc(pdata->devs_num,
				      sizeof(struct mfd_cell), GFP_KERNEL);
		if (!pdata->devs) {
			pr_err("%s failed %d\n", __func__, __LINE__);
			return -ENOMEM;
		}

		for (i = 0; i < pdata->devs_num; i++) {
			ret = of_property_read_string_index(np_s2mpb02,
				 "s2mpb02,mfd-cell", i, &pdata->devs[i].name);
			pr_info("%s mfd-cell(%d) = %s\n", __func__, i,
				pdata->devs[i].name);
			if (ret < 0) {
				pr_err("%s failed %d\n", __func__, __LINE__);
				goto err_mfd_cell;
			}
		}
	}

	return 0;

err_mfd_cell:
	kfree(pdata->devs);
	return ret;
}
#else
static int of_s2mpb02_dt(struct device *dev,
		struct s2mpb02_platform_data *pdata)
{
	return 0;
}
#endif /* CONFIG_OF */

static int s2mpb02_i2c_probe(struct i2c_client *i2c,
				const struct i2c_device_id *dev_id)
{
	struct s2mpb02_dev *s2mpb02;
	struct s2mpb02_platform_data *pdata = i2c->dev.platform_data;

	int ret = 0;
	u8 reg_data;

	pr_info("%s:%s\n", MFD_DEV_NAME, __func__);

	s2mpb02 = kzalloc(sizeof(struct s2mpb02_dev), GFP_KERNEL);
	if (!s2mpb02) {
		dev_err(&i2c->dev, "%s: Failed to alloc mem for s2mpb02\n",
							__func__);
		return -ENOMEM;
	}

	if (i2c->dev.of_node) {
		pdata = devm_kzalloc(&i2c->dev,
			sizeof(struct s2mpb02_platform_data), GFP_KERNEL);
		if (!pdata) {
			dev_err(&i2c->dev, "Failed to allocate memory\n");
			ret = -ENOMEM;
			goto err_pdata;
		}

		ret = of_s2mpb02_dt(&i2c->dev, pdata);
		if (ret < 0) {
			dev_err(&i2c->dev, "Failed to get device of_node\n");
			goto err;
		}

		i2c->dev.platform_data = pdata;
	} else
		pdata = i2c->dev.platform_data;

	s2mpb02->dev = &i2c->dev;
	s2mpb02->i2c = i2c;
	s2mpb02->irq = i2c->irq;
	if (pdata) {
		s2mpb02->pdata = pdata;

		pdata->irq_base = irq_alloc_descs(-1, 600, S2MPB02_IRQ_NR, 0);
		if (pdata->irq_base < 0) {
			pr_err("%s:%s irq_alloc_descs Fail! ret(%d)\n",
				MFD_DEV_NAME, __func__, pdata->irq_base);
			ret = -EINVAL;
			goto err;
		} else {
			s2mpb02->irq_base = pdata->irq_base;
		}

		s2mpb02->wakeup = pdata->wakeup;
	} else {
		ret = -EINVAL;
		goto err;
	}
	mutex_init(&s2mpb02->i2c_lock);

	i2c_set_clientdata(i2c, s2mpb02);

	if (s2mpb02_read_reg(s2mpb02->i2c,
			S2MPB02_REG_BST_CTRL2, &reg_data) < 0) {
		pr_err("device not found on this channel!!\n");
		ret = -ENODEV;
		goto err_irq_init;
	} else {
		if (reg_data == 0x90)
			s2mpb02->rev_num = 1;
		else
			s2mpb02->rev_num = 0;
		pr_info("%s: device id 0x%x is found\n",
				__func__, s2mpb02->rev_num);
	}

	ret = s2mpb02_irq_init(s2mpb02);
	if (ret < 0)
		goto err_irq_init;

	ret = mfd_add_devices(s2mpb02->dev, -1, pdata->devs,
			pdata->devs_num, NULL, 0, NULL);
	if (ret < 0)
		goto err_irq_init;

	device_init_wakeup(s2mpb02->dev, pdata->wakeup);
#ifdef CONFIG_DEBUG_FS
	s2mpb02_debugfs_init(s2mpb02);
#endif

	return ret;

err_irq_init:
	mutex_destroy(&s2mpb02->i2c_lock);
err:
	devm_kfree(&i2c->dev, (void *)pdata);
	s2mpb02_irq_exit(s2mpb02);
err_pdata:
	kfree(s2mpb02);

	return ret;
}

static int s2mpb02_i2c_remove(struct i2c_client *i2c)
{
	struct s2mpb02_dev *s2mpb02 = i2c_get_clientdata(i2c);

	mfd_remove_devices(s2mpb02->dev);
	s2mpb02_irq_exit(s2mpb02);
	mutex_destroy(&s2mpb02->i2c_lock);
	kfree(s2mpb02);

	return 0;
}

static const struct i2c_device_id s2mpb02_i2c_id[] = {
	{ MFD_DEV_NAME, TYPE_S2MPB02 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, s2mpb02_i2c_id);

#if defined(CONFIG_OF)
static const struct of_device_id s2mpb02_i2c_dt_ids[] = {
	{ .compatible = "s2mpb02,s2mpb02mfd" },
	{},
};
MODULE_DEVICE_TABLE(of, s2mpb02_i2c_dt_ids);
#endif /* CONFIG_OF */

#if defined(CONFIG_PM)
static int s2mpb02_suspend(struct device *dev)
{
	struct i2c_client *i2c = container_of(dev, struct i2c_client, dev);
	struct s2mpb02_dev *s2mpb02 = i2c_get_clientdata(i2c);

	if (device_may_wakeup(dev))
		enable_irq_wake(s2mpb02->irq);

	disable_irq(s2mpb02->irq);

	return 0;
}

static int s2mpb02_resume(struct device *dev)
{
	struct i2c_client *i2c = container_of(dev, struct i2c_client, dev);
	struct s2mpb02_dev *s2mpb02 = i2c_get_clientdata(i2c);

	if (device_may_wakeup(dev))
		disable_irq_wake(s2mpb02->irq);

	enable_irq(s2mpb02->irq);

	return 0;
}
#else
#define s2mpb02_suspend	NULL
#define s2mpb02_resume	NULL
#endif /* CONFIG_PM */

const struct dev_pm_ops s2mpb02_pm = {
	.suspend = s2mpb02_suspend,
	.resume = s2mpb02_resume,
};

static struct i2c_driver s2mpb02_i2c_driver = {
	.driver		= {
		.name	= MFD_DEV_NAME,
		.owner	= THIS_MODULE,
#if defined(CONFIG_PM)
		.pm	= &s2mpb02_pm,
#endif /* CONFIG_PM */
#if defined(CONFIG_OF)
		.of_match_table	= s2mpb02_i2c_dt_ids,
#endif /* CONFIG_OF */
		.suppress_bind_attrs = true,
	},
	.probe		= s2mpb02_i2c_probe,
	.remove		= s2mpb02_i2c_remove,
	.id_table	= s2mpb02_i2c_id,
};

static int __init s2mpb02_i2c_init(void)
{
	pr_info("%s:%s\n", MFD_DEV_NAME, __func__);
	return i2c_add_driver(&s2mpb02_i2c_driver);
}
/* init early so consumer devices can complete system boot */
subsys_initcall(s2mpb02_i2c_init);

static void __exit s2mpb02_i2c_exit(void)
{
	i2c_del_driver(&s2mpb02_i2c_driver);
}
module_exit(s2mpb02_i2c_exit);

MODULE_DESCRIPTION("SAMSUNG s2mpb02 multi-function core driver");
MODULE_LICENSE("GPL");
