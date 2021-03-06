
#include "corto/fmt/json/json.h"

static corto_int16 json_deserType(void *p, corto_type t, JSON_Value *v);

corto_type json_deserInlineType(JSON_Object *obj) {
    JSON_Value* type = json_object_get_value(obj, "type");
    if (!type) {
        corto_seterr("type member is mandatory for anonymous object");
        goto error;
    }
    if (json_value_get_type(type) != JSONString) {
        corto_seterr("type member of anonymous object must be a string");
        goto error;
    }
    corto_type cortoType = corto_resolve(NULL, (char*)json_value_get_string(type));
    if (!cortoType) {
        corto_seterr("type '%s' not found for anonymous object", json_value_get_string(type));
        goto error;
    }
    return cortoType;
error:
    return NULL;
}

static char* json_valueTypeToString(JSON_Value *v)
{
    switch(json_value_get_type(v)) {
    case JSONBoolean: return "boolean";
    case JSONNumber: return "number";
    case JSONString: return "string";
    case JSONObject: return "object";
    case JSONArray: return "array";
    case JSONNull: return "null";
    default: return "unknown";
    }
}

static corto_int16 json_deserBoolean(void* o, corto_primitive t, JSON_Value *v)
{
    CORTO_UNUSED(t);

    if (json_value_get_type(v) != JSONBoolean) {
        corto_seterr("expected boolean, got %s", json_valueTypeToString(v));
        goto error;
    }

    *(corto_bool *)o = json_value_get_boolean(v);

    return 0;
error:
    return -1;
}

static corto_int16 json_deserCharacter(void* o, corto_primitive t, JSON_Value *v)
{
    CORTO_UNUSED(t);

    if (json_value_get_type(v) != JSONString) {
        corto_string json = json_serialize_to_string(v);
        corto_seterr("expected string, got %s (%s)", json_valueTypeToString(v), json);
        corto_dealloc(json);
        goto error;
    } else {
        const char *str = json_value_get_string(v);
        *(corto_char*)o = *str;
    }

    return 0;
error:
    return -1;
}

static corto_int16 json_deserNumber(void* o, corto_primitive t, JSON_Value *v)
{
    corto_float64 number;

    if (json_value_get_type(v) == JSONNull) {
        number = NAN;
    } else if (json_value_get_type(v) != JSONNumber) {
        corto_string json = json_serialize_to_string(v);
        corto_seterr("expected number, got %s (%s)", json_valueTypeToString(v), json);
        corto_dealloc(json);
        goto error;
    } else {
        number = json_value_get_number(v);
    }

    corto_convert(
        corto_primitive(corto_float64_o),
        &number,
        t,
        o);

    return 0;
error:
    return -1;
}

static corto_int16 json_deserText(void* p, corto_primitive t, JSON_Value *v)
{
    CORTO_UNUSED(t);

    if (json_value_get_type(v) == JSONNull) {
        corto_setstr(p, NULL);
    } else if (json_value_get_type(v) != JSONString) {
        corto_string json = json_serialize_to_string(v);
        corto_seterr("expected string, got %s (%s)", json_valueTypeToString(v), json);
        corto_dealloc(json);
        goto error;
    } else {
        const char *s = json_value_get_string(v);
        corto_setstr(p, (corto_string)s);
    }

    return 0;
error:
    return -1;
}

static corto_int16 json_deserConstant(void* p, corto_primitive t, JSON_Value *v)
{
    const char *s = json_value_get_string(v);
    CORTO_UNUSED(t);

    if (json_value_get_type(v) != JSONString) {
        corto_seterr("expected string, got %s", json_valueTypeToString(v));
        goto error;
    }

    if (corto_convert(corto_string_o, (corto_string*)&s, t, p)) {
        goto error;
    }

    return 0;
error:
    return -1;
}

corto_bool json_deserPrimitive(void* p, corto_type t, JSON_Value *v)
{
    corto_assert(t->kind == CORTO_PRIMITIVE, "not deserializing primitive");

    corto_primitive ptype = corto_primitive(t);

    switch (ptype->kind) {
    case CORTO_BOOLEAN:
        if (json_deserBoolean(p, ptype, v)) {
            goto error;
        }
        break;
    case CORTO_CHARACTER:
        if (json_deserCharacter(p, ptype, v)) {
            goto error;
        }
        break;
    case CORTO_INTEGER:
    case CORTO_UINTEGER:
    case CORTO_FLOAT:
    case CORTO_BINARY:
        if (json_deserNumber(p, ptype, v)) {
            goto error;
        }
        break;
    case CORTO_TEXT:
        if (json_deserText(p, ptype, v)) {
            goto error;
        }
        break;
    case CORTO_ENUM:
    case CORTO_BITMASK:
        if (json_deserConstant(p, ptype, v)) {
            goto error;
        }
        break;
    }

    return 0;
error:
    return -1;
}

corto_int16 json_deserReference(void* p, corto_type t, JSON_Value* v)
{
    switch(json_value_get_type(v)) {
    case JSONString: {
        const char* reference = json_value_get_string(v);
        corto_object o = corto_resolve(NULL, (corto_string)reference);
        if (!o) {
            corto_error("unresolved reference \"%s\"", reference);
            goto error;
        }

        if (!corto_instanceof(t, o)) {
            corto_error("%s is not an instance of %s", reference, corto_idof(t));
        }

        corto_setref(p, o);
        corto_release(o);
        break;
    }
    case JSONObject: {
        JSON_Object* obj = json_value_get_object(v);

        corto_type cortoType = json_deserInlineType(obj);

        corto_object cortoObj = *(corto_object*)p;
        if (!cortoObj || (corto_typeof(cortoObj) != cortoType)) {
            cortoObj = corto_create(cortoType);
            corto_setref(p, cortoObj);
            corto_release(cortoObj);
        }
        corto_release(cortoType);

        JSON_Value* value = json_object_get_value(obj, "value");
        if (json_deserType(cortoObj, cortoType, value)) {
            goto error;
        }
        break;
    }
    case JSONNull:
        corto_setref(p, NULL);
        break;
    default:
        corto_seterr("expected string, null or object (reference), got %s", json_valueTypeToString(v));
        break;
    }

    return 0;
error:
    return -1;
}

static corto_int16 json_deserAny(void* p, corto_type t, JSON_Value *v)
{
    CORTO_UNUSED(t);

    corto_any *dst = p;
    if (json_value_get_type(v) != JSONObject) {
        corto_seterr("expected object for any, got %s", json_valueTypeToString(v));
        goto error;
    }

    JSON_Object *obj = json_value_get_object(v);
    corto_type type = json_deserInlineType(obj);
    if (!type) {
        goto error;
    }

    dst->type = type;
    dst->owner = TRUE;

    void *ptr;
    if (!type->reference) {
        ptr = corto_calloc(corto_type_sizeof(type));
        if (corto_initp(ptr, type)) {
            goto error;
        }
        dst->value = ptr;
    } else {
        ptr = &dst->value;
    }

    JSON_Value *value = json_object_get_value(obj, "value");
    if (json_deserType(ptr, type, value)) {
        goto error;
    }

    return 0;
error:
    return -1;

}

static corto_int16 json_deserItem(void *p, corto_type t, JSON_Value *v)
{
    if (t->reference) {
        if (json_deserReference(p, t, v)) {
            goto error;
        }
    } else {
        if (json_deserType(p, t, v)) {
            goto error;
        }
    }

    return 0;
error:
    return -1;
}

corto_bool json_deserMustSkip(corto_member m, void *ptr)
{
    if (corto_instanceof(corto_target_o, corto_parentof(m))) {
        corto_bool owned = corto_owned(ptr);
        corto_bool isActual = !strcmp("actual", corto_idof(m));
        if ((owned && !isActual) || (!owned && isActual)) {
            return TRUE;
        }
    }
    return FALSE;
}

static corto_int16 json_deserComposite(void* p, corto_type t, JSON_Value *v)
{
    corto_assert(t->kind == CORTO_COMPOSITE, "not deserializing composite");

    if (json_value_get_type(v) != JSONObject) {
        corto_seterr("expected object, got %s", json_valueTypeToString(v));
        goto error;
    }

    JSON_Object* o = json_value_get_object(v);
    size_t count = json_object_get_count(o);
    size_t i;
    corto_bool isUnion = corto_interface(t)->kind == CORTO_UNION;
    corto_int32 discriminator = 0;
    corto_member unionMember = NULL;

    for (i = 0; i < count; i++) {
        const char* memberName = json_object_get_name(o, i);
        corto_member member_o;

        if (!strcmp(memberName, "super")) {
            JSON_Value* value = json_object_get_value(o, memberName);
            if (json_deserType(p, corto_type(corto_interface(t)->base), value)) {
                corto_seterr("member '%s': %s", memberName, corto_lasterr());
                goto error;
            }
        } else if (!strcmp(memberName, "_d") && isUnion) {
            JSON_Value* value = json_object_get_value(o, memberName);
            if (json_deserPrimitive(&discriminator, corto_union(t)->discriminator, value)) {
                goto error;
            }
            unionMember = corto_union_findCase(t, discriminator);
            if (!unionMember) {
                corto_seterr("discriminator '%d' invalid for union '%s'",
                    discriminator, corto_fullpath(NULL, t));
            }
        } else {
            member_o = corto_interface_resolveMember(t, (char*)memberName);

            /* Ensure that we're not resolving members from a base type */
            if (!member_o || (corto_parentof(member_o) != t)) {
                corto_seterr(
                    "cannot find member '%s' in type '%s'",
                    memberName,
                    corto_fullpath(NULL, t));
                goto error;
            }

            if (isUnion && (unionMember != member_o)) {
                corto_seterr(
                    "member '%s' does not match discriminator '%d' (expected member '%s')",
                    memberName,
                    discriminator,
                    corto_idof(unionMember));
                goto error;
            } else if (isUnion) {
                corto_int32 prev = *(corto_int32*)p;
                if (prev != discriminator) {
                    corto_member prevMember = corto_union_findCase(t, prev);
                    corto_deinitp(CORTO_OFFSET(p, prevMember->offset), prevMember->type);
                    memset(CORTO_OFFSET(p, member_o->offset), 0, member_o->type->size);
                }
                *(corto_int32*)p = discriminator;
            }

            if (!json_deserMustSkip(member_o, p)) {
                JSON_Value* value = json_object_get_value(o, memberName);
                void *offset = CORTO_OFFSET(p, member_o->offset);
                if (member_o->modifiers & CORTO_OBSERVABLE) {
                    offset = *(void**)offset;
                    if (json_deserType(offset, member_o->type, value)) {
                        corto_seterr("member '%s': %s", corto_idof(member_o), corto_lasterr());
                        goto error;
                    }
                } else {
                    if (member_o->modifiers & CORTO_OPTIONAL) {
                        if (*(void**)offset) {
                            corto_deinitp(*(void**)offset, member_o->type);
                            memset(*(void**)offset, 0, member_o->type->size);
                        } else {
                            *(void**)offset = corto_calloc(member_o->type->size);
                        }
                        offset = *(void**)offset;
                    }
                    if (json_deserItem(offset, member_o->type, value)) {
                        corto_seterr("member '%s': %s", corto_idof(member_o), corto_lasterr());
                        goto error;
                    }
                }
            }
        }
    }

    return 0;
error:
    return -1;
}

void* json_deser_allocElem(void *ptr, corto_collection t, corto_int32 i)
{
    corto_int32 size = corto_type_sizeof(t->elementType);
    void *result = NULL;

    switch(t->kind) {
    case CORTO_SEQUENCE: {
        corto_objectseq *seq = ptr; /* Use random built-in sequence type */
        seq->buffer = corto_realloc(seq->buffer, (i + 1) * size);
        seq->length = i + 1;
        ptr = seq->buffer;
        memset(CORTO_OFFSET(ptr, size * i), 0, size);
    }
    case CORTO_ARRAY:
        result = CORTO_OFFSET(ptr, size * i);
        break;
    case CORTO_LIST: {
        corto_ll list = *(corto_ll*)ptr;
        if (corto_collection_requiresAlloc(t->elementType)) {
            result = corto_calloc(size);
            corto_llAppend(list, result);
        } else {
            corto_llAppend(list, NULL);
            result = corto_llGetPtr(list, corto_llSize(list) - 1);
        }
        break;
    default:
        break;
    }
    }

    return result;
}

static corto_int16 json_deserCollection(void* p, corto_type t, JSON_Value *v)
{
    corto_assert(t->kind == CORTO_COLLECTION, "not deserializing composite");
    corto_type elementType = corto_collection(t)->elementType;

    /* Deserialize elements */
    JSON_Array* a = json_value_get_array(v);
    if (!a) {
        corto_seterr("invalid array");
        goto error;
    }

    size_t count = json_array_get_count(a);
    size_t i;

    for (i = 0; i < count; i++) {
        void *elementPtr = json_deser_allocElem(p, corto_collection(t), i);
        JSON_Value *elem = json_array_get_value(a, i);
        if (json_deserType(elementPtr, elementType, elem)) {
            goto error;
        }
    }

    return 0;
error:
    return -1;
}

static corto_int16 json_deserType(void *p, corto_type t, JSON_Value *v)
{
    switch (t->kind) {
    case CORTO_VOID:
        /* Nothing to deserialize */
        break;
    case CORTO_ANY:
        if (json_deserAny(p, t, v)) {
            goto error;
        }
        break;
    case CORTO_PRIMITIVE:
        if (json_deserPrimitive(p, t, v)) {
            goto error;
        }
        break;
    case CORTO_COMPOSITE:
        if (json_deserComposite(p, t, v)) {
            goto error;
        }
        break;
    case CORTO_COLLECTION:
        if (json_deserCollection(p, t, v)) {
            goto error;
        }
        break;
    default:
        corto_seterr(
            "unsupported type, can't serialize JSON");
        break;
    }

    return 0;
error:
    return -1;
}

corto_int16 json_deserialize_from_JSON_Value(corto_value *v, JSON_Value *jsonValue) {
    void *ptr = corto_value_getPtr(v);
    corto_type type = corto_value_getType(v);

    if (json_deserType(ptr, type, jsonValue)) {
        goto error;
    }

    return 0;
error:
    return -1;
}

corto_int16 json_deserialize(corto_value *v, corto_string s)
{
    corto_assert(v != NULL, "NULL passed to json_deserialize");

    char *json = s;
    if ((json[0] != '{') && (json[1] != '[') && (json[0] != '[')) {
        corto_asprintf(&json, "{\"value\": %s}", json);
    }

    corto_trace("json: deserialize string '%s'", json);

    JSON_Value *jsonValue = json_parse_string(json);
    if (!jsonValue) {
        corto_seterr("invalid JSON '%s'", json);
        goto error;
    }

    corto_type type = corto_value_getType(v);

    if (type->kind == CORTO_PRIMITIVE) {
        JSON_Object* jsonObj = json_value_get_object(jsonValue);
        if (!jsonObj) {
            corto_seterr("json: invalid JSON for primitive value '%s'", json);
            goto error;
        }

        jsonValue = json_object_get_value(jsonObj, "value");
        if (!jsonValue) {
            corto_seterr("json: missing 'value' field for primitive value '%s'", json);
            goto error;
        }
    }

    if (json_deserialize_from_JSON_Value(v, jsonValue)) {
        corto_seterr("json: %s for JSON string '%s'", corto_lasterr(), json);
        goto error;
    }

    if (json != s) {
        corto_dealloc(json);
    }
    
    if (jsonValue) {
        json_value_free(jsonValue);
    }
    return 0;
error:
    if (json != s) {
        corto_dealloc(json);
    }

    if (jsonValue) {
        json_value_free(jsonValue);
    }
    return -1;
}
