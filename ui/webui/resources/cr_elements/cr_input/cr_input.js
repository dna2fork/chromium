// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview 'cr-input' is a component similar to native input.
 *
 * Native input attributes that are currently supported by cr-inputs are:
 *   autofocus
 *   disabled
 *   maxlength
 *   type (only 'text' and 'password')
 *   readonly
 *   pattern
 *   required
 *   value
 *   placeholder
 *   tabindex as 'tab-index' (e.g.: <cr-input tab-index="-1">)
 *
 * Additional attributes that you can use with cr-input:
 *   label
 *   auto-validate - triggers validation based on |pattern| and |required|,
 *                   whenever |value| changes.
 *   error-message - message displayed under the input when |invalid| is true.
 *   invalid
 *
 * You may pass an element into cr-input via [slot="suffix"] to be vertically
 * center-aligned with the input field, regardless of position of the label and
 * error-message. Example:
 *   <cr-input>
 *     <paper-button slot="suffix"></paper-button>
 *   </cr-input>
 */
Polymer({
  is: 'cr-input',

  properties: {
    autofocus: {
      type: Boolean,
      value: false,
      reflectToAttribute: true,
    },

    autoValidate: Boolean,

    disabled: {
      type: Boolean,
      value: false,
      reflectToAttribute: true,
      observer: 'disabledChanged_',
    },

    errorMessage: {
      type: String,
      value: '',
    },

    invalid: {
      type: Boolean,
      value: false,
      reflectToAttribute: true,
    },

    maxlength: {
      type: Number,
      reflectToAttribute: true,
    },

    pattern: {
      type: String,
      reflectToAttribute: true,
    },

    label: {
      type: String,
      value: '',
    },

    placeholder: {
      type: String,
      observer: 'placeholderChanged_',
    },

    readonly: {
      type: Boolean,
      reflectToAttribute: true,
    },

    required: {
      type: Boolean,
      reflectToAttribute: true,
    },

    tabIndex: String,

    type: {
      type: String,
      value: 'text',  // Only 'text' and 'password' are currently supported.
    },

    value: {
      type: String,
      value: '',
      notify: true,
      observer: 'onValueChanged_',
    },
  },

  hostAttributes: {
    'aria-disabled': 'false',
    role: 'textbox',
  },

  listeners: {
    'input.focus': 'onInputFocusChange_',
    'input.blur': 'onInputFocusChange_',
    'input.change': 'onInputChange_',
  },

  /** @return {!HTMLInputElement} */
  get inputElement() {
    return this.$.input;
  },

  /** @private */
  disabledChanged_: function() {
    this.setAttribute('aria-disabled', this.disabled ? 'true' : 'false');
    // In case input was focused when disabled changes.
    this.removeAttribute('focused_');
  },

  /**
   * This is necessary instead of doing <input placeholder="[[placeholder]]">
   * because if this.placeholder is set to a truthy value then removed, it
   * would show "null" as placeholder.
   * @private
   */
  placeholderChanged_: function() {
    if (this.placeholder || this.placeholder == '')
      this.inputElement.setAttribute('placeholder', this.placeholder);
    else
      this.inputElement.removeAttribute('placeholder');
  },

  focus: function() {
    if (this.shadowRoot.activeElement != this.inputElement)
      this.inputElement.focus();
  },

  /** @private */
  onValueChanged_: function() {
    if (this.autoValidate)
      this.invalid = !this.inputElement.checkValidity();
  },

  /**
   * 'change' event fires when <input> value changes and user presses 'Enter'.
   * This function helps propagate it to host since change events don't
   * propagate across Shadow DOM boundary by default.
   * @param {!Event} e
   * @private
   */
  onInputChange_: function(e) {
    this.fire('change', {sourceEvent: e});
  },

  // focused_ is used instead of :focus-within, so focus on elements within the
  // suffix slot does not trigger a change in input styles.
  onInputFocusChange_: function() {
    if (this.shadowRoot.activeElement == this.inputElement)
      this.setAttribute('focused_', '');
    else
      this.removeAttribute('focused_');
  },
});