#!/usr/bin/env python
# Generates src/style/computed_style_base{.h,.cc,_constants.h} from Blink's
# css_properties.json5, computed_style_field_aliases.json5,
# runtime_enabled_features.json5 and computed_style_extra_fields.json5, using
# Blink's own loader, field model and grouping
# (build/scripts/core/style/make_computed_style_base.py). The output is
# checked in; rerun after updating the pinned Chromium sources or the ported
# field list below:
#
#   python tools/style/make_computed_style_base.py <chromium-src> src/style
#
# Local output differs from the upstream templates where Oilpan is involved:
# groups are shared through std::shared_ptr<const T> instead of Member<T>, and
# there is no tracing, DebugDiff or FindChangedGroups. As upstream,
# ComputedStyleBuilderBase copies a group on its first write (per-group access
# flags); publishing a style resets those flags, so the builder can never write
# into a group a published style shares.
#
# Only the fields of the ported longhands are emitted. Misc groups are still
# assigned over the full upstream property list, so group placement matches
# upstream. Independent-inheritance flags (the fast path of
# PropagateIndependentInheritedProperties) are not ported.

import copy
import os
import sys
import types

# Ported fields, by CSS property name or computed_style_extra_fields name.
# 'color', 'letter-spacing' and 'word-spacing' are not separate fields: the
# resolved color lives in LegacyPaint, and the spacings in FontDescription.
PORTED_FIELDS = [
    'font',
    'line-height',
    'tab-size',
    '-webkit-text-fill-color',
    'visibility',
    'white-space-collapse',
    'text-wrap-mode',
    'EffectiveZoom',
    'HasExplicitInheritance',
    # Direction, writing mode and line breaking.
    'direction',
    'unicode-bidi',
    'writing-mode',
    'text-orientation',
    'word-break',
    '-webkit-line-break',
    'overflow-wrap',
    # Text layout.
    'text-align',
    'text-align-last',
    'text-indent',
    'hyphens',
    'hyphenate-character',
    'hyphenate-limit-chars',
    'text-transform',
    'text-combine-upright',
    'text-autospace',
    'text-wrap-style',
    'VerticalAlign',
    'VerticalAlignLength',
    # Decorations and text paint.
    'text-decoration-line',
    'text-decoration-style',
    'text-decoration-color',
    'text-decoration-thickness',
    'text-decoration-skip-ink',
    'text-underline-offset',
    'text-underline-position',
    'text-shadow',
    'TextEmphasisFill',
    'TextEmphasisMark',
    'TextEmphasisCustomMark',
    'text-emphasis-color',
    'text-emphasis-position',
    '-webkit-text-stroke-width',
    '-webkit-text-stroke-color',
    'BaseTextDecorationData',
]

# Local storage and API adjustments of upstream fields.
FIELD_OVERRIDES = {
    # Font is a value type here (no Member<Font>). The getter returns a pointer
    # like upstream, so it is hand-written on top of FontInternal().
    'font': {
        'field_template': 'external',
        'type_name': 'Font',
        'wrapper_pointer_name': None,
        'default_value': 'Font()',
        'computed_style_custom_functions': ['getter'],
    },
    # Local StyleColor subset.
    '-webkit-text-fill-color': {
        'type_name': 'StyleColorValue',
        'default_value': 'StyleColorValue::CurrentColor()',
    },
    'text-decoration-color': {
        'type_name': 'StyleColorValue',
        'default_value': 'StyleColorValue::CurrentColor()',
    },
    'text-emphasis-color': {
        'type_name': 'StyleColorValue',
        'default_value': 'StyleColorValue::CurrentColor()',
    },
    '-webkit-text-stroke-color': {
        'type_name': 'StyleColorValue',
        'default_value': 'StyleColorValue::CurrentColor()',
    },
    # ComputedStyleInitialValues::InitialTextEmphasisPosition().
    'text-emphasis-position': {
        'default_value': 'TextEmphasisPosition::kOverRight',
        'computed_style_custom_functions': [],
    },
    # Pointer fields hold std::shared_ptr<const T> instead of Member<T>.
    'text-shadow': {
        'wrapper_pointer_name': 'std::shared_ptr',
    },
    'BaseTextDecorationData': {
        'wrapper_pointer_name': 'std::shared_ptr',
    },
}

# Local fields for the pre-declaration IFC API (InlineStyle).
LOCAL_FIELDS = [
    {
        # Paint for text; its color is the computed 'color'.
        'name': '-internal-legacy-paint',
        'name_for_methods': 'LegacyPaint',
        'field_group': 'inherited',
        'field_template': 'external',
        'type_name': 'PlatformPaint',
        'default_value': 'PlatformPaint()',
    },
    {
        # Not a CSS vertical-align property.
        'name': '-internal-legacy-baseline-shift',
        'name_for_methods': 'LegacyBaselineShift',
        'field_group': 'inherited',
        'field_template': 'primitive',
        'type_name': 'LayoutUnit',
        'default_value': 'LayoutUnit()',
    },
]

# Alignment class of local types, for upstream's field ordering.
LOCAL_ALIGNMENT = {
    'PlatformPaint': 'Font',
    'StyleColorValue': 'StyleColor',
    'std::shared_ptr': 'Member',
}

INCLUDES = {
    'AppliedTextDecorationVector': 'style/applied_text_decoration.h',
    'AtomicString': 'base/text/atomic_string.h',
    'Font': 'font/font.h',
    'LayoutUnit': 'layout/layout_unit.h',
    'Length': 'geometry/length.h',
    'PlatformPaint': 'paint/platform_paint.h',
    'ShadowList': 'style/shadow_list.h',
    'StyleColorValue': 'style/style_color.h',
    'StyleHyphenateLimitChars': 'style/style_hyphenate_limit_chars.h',
    'TabSize': 'text/tab_size.h',
    'TextDecorationThickness': 'style/text_decoration_thickness.h',
    'TextDirection': 'text/text_direction.h',
    'TextEmphasisPosition': 'style/computed_style_constants.h',
    'TextUnderlinePosition': 'style/computed_style_constants.h',
    'UnicodeBidi': 'text/unicode_bidi.h',
    'WhiteSpaceCollapse': 'style/white_space.h',
    'WritingMode': 'text/writing_mode.h',
}

HEADER_COMMENT = [
    '// Generated by tools/style/make_computed_style_base.py from',
    '// blink/renderer/core/css/css_properties.json5 and',
    '// blink/renderer/core/style/computed_style_extra_fields.json5. Do not edit.',
    '// Copyright 2016 The Chromium Authors',
    '// Use of this source code is governed by a BSD-style license in LICENSE.',
]


def load(chromium_root):
    blink = os.path.join(chromium_root, 'third_party', 'blink', 'renderer')
    sys.path[:0] = [
        os.path.join(blink, 'build', 'scripts'),
        os.path.join(chromium_root, 'third_party', 'pyjson5', 'src'),
        # jinja2 and markupsafe, imported by the upstream generator module.
        os.path.join(chromium_root, 'third_party'),
    ]
    from core.css import css_properties
    from core.style import make_computed_style_base as upstream
    import keyword_utils

    paths = [
        os.path.join(blink, 'core', 'css', 'css_properties.json5'),
        os.path.join(blink, 'core', 'css',
                     'computed_style_field_aliases.json5'),
        os.path.join(blink, 'platform', 'runtime_enabled_features.json5'),
        os.path.join(blink, 'core', 'style',
                     'computed_style_extra_fields.json5'),
        os.path.join(blink, 'core', 'css', 'css_value_keywords.json5'),
    ]
    css = css_properties.CSSProperties(paths[0:4])
    properties = keyword_utils.sort_keyword_properties_by_canonical_order(
        css.longhands, paths[4], css.default_parameters) + css.extra_fields

    # ComputedStyleBaseWriter.__init__: misc groups over every field.
    bitfield_properties = {
        p.name.original
        for p in properties if p.field_template is not None
        and int(upstream._find_size_for_property(p) or 64) < 8
    }
    upstream._evaluate_misc_group(properties, bitfield_properties, False)
    upstream._evaluate_misc_group(properties, bitfield_properties, True)

    by_name = {p.name.original: p for p in properties}
    selected = []
    for name in PORTED_FIELDS:
        property_ = copy.copy(by_name[name])
        property_.independent = False
        for key, value in FIELD_OVERRIDES.get(name, {}).items():
            setattr(property_, key, value)
        selected.append(property_)
    for local in LOCAL_FIELDS:
        name_for_methods = local['name_for_methods']
        selected.append(
            types.SimpleNamespace(
                name=types.SimpleNamespace(original=local['name']),
                name_for_methods=name_for_methods,
                field_group=local['field_group'],
                field_template=local['field_template'],
                type_name=local['type_name'],
                wrapper_pointer_name=None,
                field_size=None,
                default_value=local['default_value'],
                inherited=True,
                independent=False,
                semi_independent_variable=False,
                invalidate=[],
                derived_from=None,
                reset_on_new_style=False,
                custom_compare=False,
                mutable=False,
                getter=name_for_methods,
                setter='Set' + name_for_methods,
                initial='Initial' + name_for_methods,
                computed_style_custom_functions=[],
                computed_style_protected_functions=[],
                keywords=None,
                include_paths=[]))

    for local_type, upstream_type in LOCAL_ALIGNMENT.items():
        order = upstream.ALIGNMENT_ORDER
        if local_type not in order:
            order.insert(order.index(upstream_type) + 1, local_type)
    root = upstream._create_groups(selected)
    enums = upstream._create_enums(selected)
    # css_value_id_mappings_generated.h: every keyword field, including the
    # enums defined outside the generated constants (TextDirection, ...).
    from name_utilities import enum_key_for_css_keyword
    mappings = {}
    for property_ in selected:
        keywords = getattr(property_, 'keywords', None)
        if not keywords or property_.type_name in MAPPING_EXCLUDED_TYPES:
            continue
        if property_.field_template not in ('keyword', 'multi_keyword',
                                            'primitive'):
            continue
        keys = [enum_key_for_css_keyword(k) for k in keywords]
        if property_.type_name in mappings:
            assert set(mappings[property_.type_name]) == set(keys)
            continue
        mappings[property_.type_name] = keys
    return root, enums, sorted(mappings.items())


# Keyword fields whose keywords do not name their enum values.
MAPPING_EXCLUDED_TYPES = {'unsigned'}


def check_supported(root):
    for field in root.all_fields:
        assert field.field_template in ('keyword', 'multi_keyword',
                                        'primitive', 'external',
                                        'monotonic_flag', 'pointer'), field.name
        assert (field.wrapper_pointer_name == 'std::shared_ptr'
                if field.field_template == 'pointer' else
                not field.wrapper_pointer_name), field.name
        assert not field.derived_from, field.name
        assert not field.mutable, field.name
        assert field.is_property, field.name
        if field.group.name:
            assert not field.reset_on_new_style, field.name
    for group in root.all_subgroups:
        inherited = [f.is_inherited for f in group.all_fields]
        assert all(inherited) or not any(inherited), \
            'Group %s mixes inherited and non-inherited fields' % group.name


# templates/fields/field.tmpl


def encode(field, value):
    return 'static_cast<unsigned>(%s)' % value if field.is_bit_field else value


def decode(field, value):
    if field.is_bit_field:
        return 'static_cast<%s>(%s)' % (field.type_name, value)
    return value


def by_value(field):
    return field.is_bit_field or field.field_template == 'primitive'


def is_pointer(field):
    return field.field_template == 'pointer'


def const_ref(field):
    if is_pointer(field):
        return 'const %s*' % field.type_name
    return field.type_name if by_value(field) else 'const %s&' % field.type_name


def pointer_type(field):
    return 'std::shared_ptr<const %s>' % field.type_name


def nonconst_ref(field):
    return field.type_name if by_value(field) else '%s&' % field.type_name


def getter_method_name(field):
    if 'getter' in field.computed_style_custom_functions:
        return field.internal_getter_method_name
    return field.getter_method_name


def setter_method_name(field):
    if 'setter' in field.computed_style_custom_functions:
        return field.internal_setter_method_name
    return field.setter_method_name


def resetter_method_name(field):
    if 'resetter' in field.computed_style_custom_functions:
        return field.internal_resetter_method_name
    return field.resetter_method_name


def group_path(group):
    return '->'.join(g.member_name for g in group.path_without_root())


def getter_expression(field):
    if not field.group.parent:
        return 'data_.' + field.name
    return group_path(field.group) + '->' + field.name


def nested_group_access(group):
    flag = 'access_.' + group.member_name
    if group.parent.name:
        return 'Access(%s->%s, %s)' % (nested_group_access(group.parent),
                                       group.member_name, flag)
    return 'Access(%s, %s)' % (group.member_name, flag)


def setter_expression(field):
    if field.group.name:
        return nested_group_access(field.group) + '->' + field.name
    return 'data_.' + field.name


def assign_if_changed(field, value, move=False):
    stored = 'std::move(%s)' % value if move else value
    if field.group.name:
        return [
            'if (!(%s == %s))' % (getter_expression(field), value),
            '  %s = %s;' % (setter_expression(field), stored),
        ]
    return ['data_.%s = %s;' % (field.name, stored)]


def method(signature, body):
    return [signature + ' {'] + ['  ' + line for line in body] + ['}']


# templates/fields/{base,keyword,primitive,external,monotonic_flag}.tmpl


def getter_method(field, visibility):
    if field.getter_visibility != visibility:
        return []
    if is_pointer(field):
        # Local: the shared pointer too, for values that propagate to other
        # styles (upstream copies the Member).
        return method(
            '%s %s() const' % (const_ref(field), getter_method_name(field)),
            ['return %s.get();' % getter_expression(field)]) + method(
                'const %s& Shared%s() const' %
                (pointer_type(field), getter_method_name(field)),
                ['return %s;' % getter_expression(field)])
    return method(
        '%s %s() const' % (const_ref(field), getter_method_name(field)),
        ['return %s;' % decode(field, getter_expression(field))])


def setter_method(field, visibility):
    if field.setter_visibility != visibility:
        return []
    if is_pointer(field):
        # templates/fields/pointer.tmpl: takes the wrapped pointer and
        # compares identity, like move_if_changed.
        return method(
            'void %s(%s v)' % (setter_method_name(field), pointer_type(field)),
            assign_if_changed(field, 'v', move=True))
    return method(
        'void %s(%s v)' % (setter_method_name(field), const_ref(field)),
        assign_if_changed(field, encode(field, 'v')))


def move_method(field, visibility):
    if field.setter_visibility != visibility or is_pointer(field):
        return []
    return method(
        'void %s(%s&& v)' % (setter_method_name(field), field.type_name),
        assign_if_changed(field, 'v', move=True))


def resetter_method(field, visibility):
    if field.resetter_visibility != visibility:
        return []
    return method(
        'void %s()' % resetter_method_name(field), [
            '%s = %s;' %
            (setter_expression(field), encode(field, field.default_value))
        ])


def mutable_method(field):
    # Upstream returns bit fields and primitives by value, which mutates
    # nothing but still copies the group. Those accessors are omitted.
    if by_value(field) or is_pointer(field):
        return []
    return method(
        '%s %s()' % (nonconst_ref(field), field.internal_mutable_method_name),
        ['return %s;' % setter_expression(field)])


def field_methods(field, visibility, builder):
    if not builder:
        return getter_method(field, visibility)
    template = field.field_template
    if template == 'monotonic_flag':
        if visibility == 'public':
            return getter_method(field, visibility) + method(
                'void %s()' % field.setter_method_name,
                assign_if_changed(field, encode(field, 'true')))
        return setter_method(field, visibility)
    lines = getter_method(field, visibility) + setter_method(
        field, visibility)
    if template in ('external', 'pointer'):
        lines += move_method(field, visibility)
    lines += resetter_method(field, visibility)
    if visibility == 'protected':
        lines += mutable_method(field)
    return lines


def all_field_methods(root, visibility, builder):
    lines = []
    for field in sorted(root.all_fields, key=lambda f: f.name):
        methods = field_methods(field, visibility, builder)
        if methods:
            lines += ['// ' + field.property_name] + methods + ['']
    return lines


def declare_storage(field):
    if field.is_bit_field:
        return 'unsigned %s : %d; // %s' % (field.name, field.size,
                                            field.type_name)
    if is_pointer(field):
        return '%s %s;' % (pointer_type(field), field.name)
    return '%s %s;' % (field.type_name, field.name)


def compare_expr(field, expression, other):
    # templates/fields/field.tmpl compare_expr(): pointers compare values.
    if is_pointer(field):
        return 'base::ValuesEquivalent(%s, %s)' % (expression, other)
    return '%s == %s' % (expression, other)


def fieldwise_compare(group, fields):
    terms = []
    for subgroup in group.subgroups:
        expression = group_path(subgroup)
        compared = [f for f in subgroup.all_fields if not f.custom_compare]
        if all(f in fields for f in compared):
            terms.append('base::ValuesEquivalent(%s, o.%s)' %
                         (expression, expression))
        elif any(f in fields for f in compared):
            terms.append('(%s.get() == o.%s.get() || (%s))' %
                         (expression, expression, ' && '.join(
                             fieldwise_compare(subgroup, fields))))
    for field in group.fields:
        if not field.custom_compare and field in fields:
            expression = getter_expression(field)
            terms.append(compare_expr(field, expression, 'o.' + expression))
    return terms


def compare_method(name, root, fields):
    terms = fieldwise_compare(root, fields) or ['true']
    body = ['return ' + terms[0]] + ['    && ' + t for t in terms[1:]]
    body[-1] += ';'
    return method('bool %s(const ComputedStyleBase& o) const' % name, body)


def declare_group_class(group):
    lines = []
    for subgroup in group.subgroups:
        lines += declare_group_class(subgroup) + ['']
    members = []
    for subgroup in group.subgroups:
        members.append('std::shared_ptr<const %s> %s;' %
                       (subgroup.type_name, subgroup.member_name))
    members += [declare_storage(field) for field in group.fields]
    compare = ['return true']
    compare += [
        '    && base::ValuesEquivalent(%s, other.%s)' %
        (s.member_name, s.member_name) for s in group.subgroups
    ]
    compare += [
        '    && ' + compare_expr(f, f.name, 'other.' + f.name)
        for f in group.fields if not f.custom_compare
    ]
    compare[-1] += ';'
    name = group.type_name
    lines += ['class %s {' % name, 'public:', '  %s();' % name]
    lines += ['  %s(const %s&) = default;' % (name, name), '']
    lines += ['  ' + line for line in method(
        'bool operator==(const %s& other) const' % name, compare)]
    lines += [
        '  bool operator!=(const %s& other) const { return !(*this == other); }'
        % name, ''
    ]
    lines += ['  ' + member for member in members]
    lines += ['};']
    return lines


def define_group_class(group):
    lines = []
    for subgroup in group.subgroups:
        lines += define_group_class(subgroup) + ['']
    initializers = [
        '%s(std::make_shared<%s>())' % (s.member_name, s.type_name)
        for s in group.subgroups
    ]
    initializers += [
        '%s(%s)' % (f.name, encode(f, f.default_value)) for f in group.fields
    ]
    lines.append('ComputedStyleBase::%s::%s()' % (group.type_name,
                                                   group.type_name))
    lines += ['    %s %s' % (':' if i == 0 else ',', initializer)
              for i, initializer in enumerate(initializers)]
    lines.append('{}')
    return lines


def initializer_list(entries):
    return ['    %s %s' % (':' if i == 0 else ',', entry)
            for i, entry in enumerate(entries)]


def data_initializer(values):
    lines = ['data_{']
    lines += ['        %s,' % value for value in values]
    lines[-1] = lines[-1].rstrip(',')
    lines.append('      }')
    return lines


def group_source(group):
    if all(f.is_inherited for f in group.all_fields):
        return 'parent_style.' + group.member_name
    return 'source_for_noninherited.' + group.member_name


def field_source(field):
    if field.reset_on_new_style:
        return encode(field, field.default_value)
    if field.is_inherited:
        return 'parent_style.data_.' + field.name
    return 'source_for_noninherited.data_.' + field.name


def generate_constants(enums):
    lines = HEADER_COMMENT + ['#pragma once', '', '#include <stdint.h>', '']
    lines += ['namespace bkfont {', '']
    for enum in enums:
        assert enum.set_type in (None, 'multi'), enum.type_name
        underlying = ('unsigned' if enum.set_type or len(enum.values) > 256
                      else 'uint8_t')
        lines.append('enum class %s : %s {' % (enum.type_name, underlying))
        if enum.set_type == 'multi':
            # templates/core/style/computed_style_base_constants.h.tmpl
            lines += [
                '  %s = %d,' % (value, 0 if i == 0 else 2**(i - 1))
                for i, value in enumerate(enum.values)
            ]
            lines += ['};', '']
            name = enum.type_name
            lines.append('static const int k%sBits = %d;' %
                         (name, len(enum.values) - 1))
            lines.append('')
            for op in ('|', '^', '&'):
                lines += [
                    'inline %s operator%s(%s a, %s b) {' % (name, op, name,
                                                            name),
                    '  return static_cast<%s>(static_cast<unsigned>(a) %s '
                    'static_cast<unsigned>(b));' % (name, op),
                    '}',
                    'inline %s& operator%s=(%s& a, %s b) {' % (name, op,
                                                               name, name),
                    '  return a = a %s b;' % op,
                    '}',
                    '',
                ]
            lines += [
                'inline %s operator~(%s x) {' % (name, name),
                '  return static_cast<%s>(~static_cast<unsigned>(x));' % name,
                '}',
                '',
            ]
            continue
        lines += ['  %s,' % value for value in enum.values]
        lines += ['  kMaxEnumValue = %s,' % enum.values[-1], '};', '']
    lines += ['} // namespace bkfont', '']
    return lines


def generate_header(root):
    types_used = sorted({f.type_name for f in root.all_fields})
    includes = sorted({INCLUDES[t] for t in types_used if t in INCLUDES} |
                      {'base/memory/values_equivalent.h',
                       'style/computed_style_base_constants.h'})
    properties = [f for f in root.all_fields if f.is_property]
    inherited = [f for f in properties if f.is_inherited]
    independent = [
        f for f in inherited
        if f.is_independent and not f.is_semi_independent_variable
    ]
    non_independent = [
        f for f in inherited
        if not f.is_independent and not f.is_semi_independent_variable
    ]
    non_inherited = [f for f in properties if not f.is_inherited]

    lines = HEADER_COMMENT + ['#pragma once', '', '#include <memory>',
                              '#include <utility>', '']
    lines += ['#include "%s"' % path for path in includes]
    lines += [
        '',
        'namespace bkfont {',
        '',
        'class ComputedStyleBuilderBase;',
        '',
        '// The generated portion of ComputedStyle. Fields are stored in a tree',
        '// of groups; styles and builders share a group until one of them',
        '// writes to it. A published ComputedStyleBase is never written.',
        'class ComputedStyleBase {',
        'public:',
    ]
    body = []
    body += compare_method('IndependentInheritedEqual', root, independent)
    body += ['']
    body += compare_method('NonIndependentInheritedEqual', root,
                           non_independent)
    body += ['']
    body += method('bool InheritedEqual(const ComputedStyleBase& o) const', [
        'return IndependentInheritedEqual(o) && NonIndependentInheritedEqual(o);'
    ])
    body += ['']
    body += compare_method('NonInheritedEqual', root, non_inherited)
    body += ['']
    body += all_field_methods(root, 'public', builder=False)
    for subgroup in root.subgroups:
        body += declare_group_class(subgroup) + ['']
    lines += ['  ' + line if line else '' for line in body]
    lines += ['protected:']
    body = [
        'ComputedStyleBase();',
        'ComputedStyleBase(const ComputedStyleBase&) = default;',
        'ComputedStyleBase& operator=(const ComputedStyleBase&) = delete;',
        '// Publishes the builder\'s groups and resets its access flags.',
        'explicit ComputedStyleBase(const ComputedStyleBuilderBase&);',
        '',
    ]
    body += all_field_methods(root, 'protected', builder=False)
    body += ['struct Data {']
    body += ['  ' + declare_storage(f) for f in root.fields]
    body += ['};']
    lines += ['  ' + line if line else '' for line in body]
    lines += ['', 'private:']
    body = ['friend class ComputedStyleBuilderBase;', '']
    body += [
        'std::shared_ptr<const %s> %s;' % (s.type_name, s.member_name)
        for s in root.subgroups
    ]
    body += ['Data data_;']
    lines += ['  ' + line if line else '' for line in body]
    lines += ['};', '']

    lines += [
        'class ComputedStyleBuilderBase {',
        'public:',
    ]
    body = all_field_methods(root, 'public', builder=True)
    lines += ['  ' + line if line else '' for line in body]
    lines += ['protected:']
    body = [
        'ComputedStyleBuilderBase() = delete;',
        'explicit ComputedStyleBuilderBase(const ComputedStyleBase&);',
        'ComputedStyleBuilderBase(const ComputedStyleBase& source_for_noninherited,',
        '                         const ComputedStyleBase& parent_style);',
        '// Two builders must not own the same writable group.',
        'ComputedStyleBuilderBase(const ComputedStyleBuilderBase&) = delete;',
        'ComputedStyleBuilderBase(ComputedStyleBuilderBase&&) = default;',
        'ComputedStyleBuilderBase& operator=(const ComputedStyleBuilderBase&) = delete;',
        'ComputedStyleBuilderBase& operator=(ComputedStyleBuilderBase&&) = default;',
        '',
    ]
    body += all_field_methods(root, 'protected', builder=True)
    body += method('void ResetAccess() const', ['access_ = {};'])
    lines += ['  ' + line if line else '' for line in body]
    lines += ['', 'private:']
    body = ['friend class ComputedStyleBase;', '']
    body += [
        '// Set once this builder holds a private copy of the group. The copy',
        '// was made by this builder and is not yet shared with any style.',
        'mutable struct {',
    ]
    body += [
        '  bool %s = false;' % g.member_name
        for g in sorted(root.all_subgroups, key=lambda g: g.name)
    ]
    body += ['} access_;', '']
    body += [
        'template <typename T>',
        'static T* Access(std::shared_ptr<const T>& data, bool& access_flag) {',
        '  if (!access_flag) {',
        '    data = std::make_shared<T>(*data);',
        '    access_flag = true;',
        '  }',
        '  // The object was created non-const by the copy above.',
        '  return const_cast<T*>(data.get());',
        '}',
        '',
    ]
    body += [
        'using %s = ComputedStyleBase::%s;' % (s.type_name, s.type_name)
        for s in root.subgroups
    ]
    body += ['']
    body += [
        'std::shared_ptr<const %s> %s;' % (s.type_name, s.member_name)
        for s in root.subgroups
    ]
    body += ['ComputedStyleBase::Data data_;']
    lines += ['  ' + line if line else '' for line in body]
    lines += ['};', '', '} // namespace bkfont', '']
    return lines


def generate_source(root):
    lines = HEADER_COMMENT + ['#include "computed_style_base.h"', '']
    lines += ['namespace bkfont {', '']

    entries = [
        '%s(std::make_shared<%s>())' % (s.member_name, s.type_name)
        for s in root.subgroups
    ]
    lines.append('ComputedStyleBase::ComputedStyleBase()')
    lines += initializer_list(entries + data_initializer(
        ['%s /* %s */' % (encode(f, f.default_value), f.name)
         for f in root.fields])[:1])
    lines += data_initializer(
        ['%s /* %s */' % (encode(f, f.default_value), f.name)
         for f in root.fields])[1:]
    lines += ['{}', '']

    entries = ['%s(builder.%s)' % (s.member_name, s.member_name)
               for s in root.subgroups] + ['data_(builder.data_)']
    lines.append(
        'ComputedStyleBase::ComputedStyleBase(const ComputedStyleBuilderBase& builder)')
    lines += initializer_list(entries)
    lines += ['{', '  builder.ResetAccess();', '}', '']

    for subgroup in root.subgroups:
        lines += define_group_class(subgroup) + ['']

    entries = ['%s(style.%s)' % (s.member_name, s.member_name)
               for s in root.subgroups] + ['data_(style.data_)']
    lines.append(
        'ComputedStyleBuilderBase::ComputedStyleBuilderBase(const ComputedStyleBase& style)'
    )
    lines += initializer_list(entries)
    lines += ['{}', '']

    entries = ['%s(%s)' % (s.member_name, group_source(s))
               for s in root.subgroups]
    data = data_initializer([field_source(f) for f in root.fields])
    lines += [
        'ComputedStyleBuilderBase::ComputedStyleBuilderBase(',
        '    const ComputedStyleBase& source_for_noninherited,',
        '    const ComputedStyleBase& parent_style)',
    ]
    lines += initializer_list(entries + data[:1])
    lines += data[1:]
    lines += ['{}', '', '} // namespace bkfont', '']
    return lines


def generate_mappings(mappings):
    # build/scripts/core/css/make_css_value_id_mappings.py: a keyword and
    # the enum value of the same name correspond.
    lines = [
        '// Generated by tools/style/make_computed_style_base.py from',
        '// blink/renderer/core/css/css_properties.json5. Do not edit.',
        '// Copyright 2016 The Chromium Authors',
        '// Use of this source code is governed by a BSD-style license in LICENSE.',
        '#pragma once',
        '',
        '#include "base/notreached.h"',
        '#include "style/computed_style_base.h"',
        '#include "style/css_value_keywords.h"',
        '',
        'namespace bkfont {',
        'namespace detail {',
        '',
        'template <class T>',
        'T cssValueIDToPlatformEnumGenerated(CSSValueID);',
        '',
    ]
    for type_name, keys in mappings:
        lines += [
            'template <>',
            'inline %s cssValueIDToPlatformEnumGenerated(CSSValueID v) {' %
            type_name,
            '  switch (v) {',
        ]
        for key in keys:
            lines.append('    case CSSValueID::%s: return %s::%s;' %
                         (key, type_name, key))
        lines += ['    default: break;', '  }', '  NOTREACHED();', '}', '']
        lines += [
            'inline CSSValueID platformEnumToCSSValueIDGenerated(%s v) {' %
            type_name,
            '  switch (v) {',
        ]
        for key in keys:
            lines.append('    case %s::%s: return CSSValueID::%s;' %
                         (type_name, key, key))
        lines += [
            '    default: break;', '  }', '  NOTREACHED();', '}', ''
        ]
    lines += ['} // namespace detail', '} // namespace bkfont', '']
    return lines


def main(chromium_root, output_dir):
    root, enums, mappings = load(chromium_root)
    check_supported(root)
    outputs = (
        ('css_value_id_mappings_generated.h', generate_mappings(mappings)),
        ('computed_style_base_constants.h', generate_constants(enums)),
        ('computed_style_base.h', generate_header(root)),
        ('computed_style_base.cc', generate_source(root)),
    )
    for name, lines in outputs:
        with open(os.path.join(output_dir, name), 'w', encoding='utf-8',
                  newline='\n') as output:
            output.write('\n'.join(lines))


if __name__ == '__main__':
    if len(sys.argv) != 3:
        sys.exit(
            'usage: make_computed_style_base.py <chromium-src> <output-dir>')
    main(os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2]))
