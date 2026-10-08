// SPDX-License-Identifier: GPL-2.0-only
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

struct led_button_data {
	struct gpio_desc *led;
};

static int led_button_probe(struct platform_device *pdev)
{
	struct led_button_data *data;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->led = devm_gpiod_get(&pdev->dev, "led", GPIOD_OUT_LOW);
	if (IS_ERR(data->led))
		return dev_err_probe(&pdev->dev, PTR_ERR(data->led),
				     "Failed to request LED GPIO\n");

	platform_set_drvdata(pdev, data);
	gpiod_set_value_cansleep(data->led, 1);
	dev_info(&pdev->dev, "LED on\n");
	return 0;
}

static void led_button_remove(struct platform_device *pdev)
{
	struct led_button_data *data = platform_get_drvdata(pdev);

	gpiod_set_value_cansleep(data->led, 0);
	dev_info(&pdev->dev, "LED off\n");
}

static const struct of_device_id led_button_of_match[] = {
	{ .compatible = "custom,led-button" },
	{ }
};
MODULE_DEVICE_TABLE(of, led_button_of_match);

static struct platform_driver led_button_driver = {
	.probe = led_button_probe,
	.remove_new = led_button_remove,
	.driver = {
		.name = "led-button",
		.of_match_table = led_button_of_match,
	},
};
module_platform_driver(led_button_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Raspberry Pi LED/button teaching driver: LED-only stage");
