# ArcForge System Analysis

## Overview

The ArcForge system is a deterministic visual vocabulary and rendering pipeline for the ARCADE-1 platform. It's designed to ensure that all visual assets can be compiled and rendered in a strictly deterministic manner, which is essential for maintaining golden hash consistency across different builds and platforms.

## Core Components

### 1. System Architecture (ARCHITECTURE.md)
The system follows a structured compilation pipeline:

1. **Input**: JSON specifications for assets, archetypes, and recipes
2. **Validation**: Schema validation using JSON Schema
3. **Registry**: Asset registry with archetype and recipe definitions
4. **Compilation**: Translation to intermediate representation (IR)
5. **Rasterization**: Deterministic rasterization using integer-only math
6. **Post-processing**: Final processing and export

### 2. Schema Validation (schema/)
The system uses strict JSON schemas for all components:
- `asset.schema.json`: Core asset specification
- `archetype.schema.json`: Archetype definition schema  
- `recipe.schema.json`: Recipe specification schema

### 3. Asset Specification (examples/)
Assets are defined using a semantic specification that can be compiled to pixels through deterministic steps.

### 4. Archetypes and Recipes (registry/)
- **Archetypes** define base structures and properties
- **Recipes** define how to combine and render assets

## Deterministic Rendering Requirements

The system enforces strict determinism:
- All rendering uses integer-only math (no floats)
- No randomization or timestamps
- No non-deterministic operations
- Byte-for-byte reproducibility across implementations

## Compilation Pipeline

1. **Schema Validation**: All inputs must conform to JSON schemas
2. **Registry Validation**: Ensures archetype and recipe definitions are valid
3. **Compilation to IR**: Assets are compiled to intermediate representation
4. **Rasterization**: Conversion to pixel data using deterministic math
5. **Post-processing**: Final processing and export

## Key Design Principles

- **Typed Parameters**: All parameters are strictly typed (int, float, bool, string)
- **Expression Language**: Supports mathematical expressions with resolution order
- **Shapes and Layers**: Asset composition through defined shapes and layers
- **Slots**: Defined areas for specific components
- **Animation Behavior**: Support for animation through parameter changes

## Reference Implementation

The Python reference tool (`arcforge_ref.py`) serves as the oracle for validation, with C compiler implementations expected to reproduce results byte-for-byte.

## Testing Approach

The system includes comprehensive testing:
- Schema validation tests
- Registry linting 
- Asset assertions
- Golden hash validation
- Human visual review

## Error Handling

The system provides structured diagnostics with JSON Patch fixes for common issues, ensuring developers get meaningful error messages with suggestions for corrections.