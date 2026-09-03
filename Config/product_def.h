#ifndef PRODUCT_DEF_H
#define PRODUCT_DEF_H

/*
 * 产品物模型/数据点定义：
 * 属性表由PROPERTY_DEF宏展开生成(编译期注册到cloud物模型引擎)，
 * 传感器采集与云端属性共用同一份定义，新增属性只需加一行。
 *
 * PROPERTY_DEF(_KEY_, _TYPE_, _WRITABLE_, _INT_DEF_, _FLOAT_DEF_)
 * _KEY_:      属性名(云端物模型一致)
 * _TYPE_:     PROP_INT/PROP_FLOAT/PROP_BOOL
 * _WRITABLE_: 1云端可下发, 0只读上报
 * _INT_DEF_:  PROP_INT/PROP_BOOL默认整数值
 * _FLOAT_DEF_:PROP_FLOAT默认浮点值
 */

/* 属性值类型(cloud物模型引擎与App物模型共用) */
enum {
    PROP_INT,
    PROP_FLOAT,
    PROP_BOOL
};

#define PRODUCT_PROPERTY_TABLE \
    PROPERTY_DEF("CSQ",                PROP_INT,   0, 20,   0.0f) \
    PROPERTY_DEF("hot_valve",          PROP_INT,   1, 10,   0.0f) \
    PROPERTY_DEF("humidity_value",     PROP_INT,   0, 45,   0.0f) \
    PROPERTY_DEF("temp_value",         PROP_FLOAT, 0, 0,    25.5f) \
    PROPERTY_DEF("smoke_alarm",        PROP_BOOL,  0, 0,    0.0f)  \
    PROPERTY_DEF("smoke_value",        PROP_INT,   0, 0,    0.0f)

#endif /* PRODUCT_DEF_H */
